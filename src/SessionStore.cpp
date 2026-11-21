#include "SessionStore.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariantMap>
#include <algorithm>

namespace {
qint64 nowUtc() { return QDateTime::currentSecsSinceEpoch(); }
}

SessionStore::SessionStore(const QString &path) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        m_error = "Could not create the private chat database folder.";
        return;
    }
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        m_error = "Qt SQLite driver is missing; chat history stays in memory.";
        return;
    }
    m_connectionName = "ember-chat-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return;
    }
    // WAL allows safe crash recovery. No audio/video files are stored here.
    for (const auto &sql : {
             "PRAGMA foreign_keys=ON",
             "PRAGMA busy_timeout=3000",
             "PRAGMA journal_mode=WAL",
             "PRAGMA synchronous=NORMAL",
              "PRAGMA wal_autocheckpoint=256",
              "PRAGMA journal_size_limit=2097152",
              "PRAGMA secure_delete=ON",
             "CREATE TABLE IF NOT EXISTS sessions("
             "id TEXT PRIMARY KEY, title TEXT NOT NULL DEFAULT 'New conversation',"
             "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)",
             "CREATE TABLE IF NOT EXISTS messages("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,"
             "role TEXT NOT NULL CHECK(role IN ('you','ember')),"
             "body TEXT NOT NULL, created_at INTEGER NOT NULL)",
             "CREATE TABLE IF NOT EXISTS session_notes("
             "session_id TEXT PRIMARY KEY REFERENCES sessions(id) ON DELETE CASCADE,"
             "body TEXT NOT NULL)",
             "CREATE TABLE IF NOT EXISTS session_asr_hints("
             "session_id TEXT PRIMARY KEY REFERENCES sessions(id) ON DELETE CASCADE,"
             "body TEXT NOT NULL)",
             "CREATE INDEX IF NOT EXISTS ix_messages_session ON messages(session_id,id)",
             "CREATE INDEX IF NOT EXISTS ix_sessions_updated ON sessions(updated_at DESC)"
         }) {
        if (!execute(QString::fromUtf8(sql))) return;
    }
    m_ready = true;
}
SessionStore::~SessionStore() {
    if (m_db.isValid()) m_db.close();
    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty())
        QSqlDatabase::removeDatabase(m_connectionName);
}
bool SessionStore::execute(const QString &statement) {
    QSqlQuery query(m_db);
    if (!query.exec(statement)) {
        m_error = query.lastError().text();
        return false;
    }
    return true;
}
QString SessionStore::createSession() {
    if (!m_ready) return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO sessions(id,title,created_at,updated_at) VALUES(?,?,?,?)");
    const qint64 now = nowUtc();
    query.addBindValue(id);
    query.addBindValue("New conversation");
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return {};
    }
    return id;
}
QVariantList SessionStore::sessions(int limit) const {
    QVariantList result;
    if (!m_ready) return result;
    QSqlQuery query(m_db);
    query.prepare("SELECT id,title,updated_at FROM sessions "
                  "ORDER BY updated_at DESC, rowid DESC LIMIT ?");
    query.addBindValue(std::clamp(limit, 1, 200));
    if (!query.exec()) return result;
    while (query.next()) {
        result.append(QVariantMap{
            {"id", query.value(0).toString()},
            {"title", query.value(1).toString()},
            {"updated", query.value(2).toLongLong()}
        });
    }
    return result;
}
QVariantList SessionStore::messages(const QString &session, int limit) const {
    QVariantList reverse;
    if (!m_ready || session.isEmpty()) return reverse;
    QSqlQuery query(m_db);
    query.prepare("SELECT role,body FROM messages WHERE session_id=? "
                  "ORDER BY id DESC LIMIT ?");
    query.addBindValue(session);
    query.addBindValue(std::clamp(limit, 1, 500));
    if (!query.exec()) return reverse;
    while (query.next()) {
        reverse.append(QVariantMap{{"role", query.value(0).toString()},
                                   {"body", query.value(1).toString()}});
    }
    std::reverse(reverse.begin(), reverse.end());
    return reverse;
}
QVariantList SessionStore::search(const QString &text, int limit) const {
    QVariantList result;
    const QString term = text.trimmed();
    if (!m_ready || term.isEmpty() || term.size() > 120) return result;
    // Quote the LIKE wildcard and escape character: a literal 100% search
    // must not turn into an unbounded wildcard query.
    QString escaped = term;
    escaped.replace("\\", "\\\\");
    escaped.replace("%", "\\%");
    escaped.replace("_", "\\_");
    const QString pattern = "%" + escaped + "%";
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT s.id, s.title, s.updated_at,"
        "COALESCE((SELECT substr(m.body,1,160) FROM messages m "
        "WHERE m.session_id=s.id AND m.body LIKE ? ESCAPE '\\' "
        "ORDER BY m.id DESC LIMIT 1),'') "
        "FROM sessions s WHERE s.title LIKE ? ESCAPE '\\' "
        "OR EXISTS(SELECT 1 FROM messages m WHERE m.session_id=s.id "
        "AND m.body LIKE ? ESCAPE '\\') "
        "ORDER BY s.updated_at DESC, s.rowid DESC LIMIT ?");
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 100));
    if (!query.exec()) return result;
    while (query.next()) {
        result.append(QVariantMap{
            {"id", query.value(0).toString()},
            {"title", query.value(1).toString()},
            {"updated", query.value(2).toLongLong()},
            {"snippet", query.value(3).toString().simplified()}
        });
    }
    return result;
}
QString SessionStore::historicalExcerpts(const QString &session,
                                         const QString &question,
                                         int maxResults) const {
    if (!m_ready || session.isEmpty() || session.size() > 64 ||
        question.trimmed().isEmpty() || question.size() > 4096) return {};
    // Avoid an embeddings index or long-lived duplicate text on the disk.
    static const QSet<QString> ignored{
        "the", "and", "this", "that", "then", "from", "with", "about",
        "there", "which", "what", "when", "where", "would", "could",
        "should", "have", "were", "they", "them", "your", "you",
        "for", "are", "was", "how", "why", "can", "did", "does",
        "please", "tell", "remember", "again", "before", "after",
        "что", "как", "это", "этот", "этом", "этого", "мне", "тебе",
        "для", "про", "где", "когда", "кто", "она", "они", "его",
        "еще", "ещё", "надо", "нужно", "было", "был", "будет",
        "можешь", "можно", "хочу", "теперь", "раньше", "тогда"
    };
    QStringList terms;
    const QRegularExpression words(R"([\p{L}\p{N}]{3,32})");
    auto match = words.globalMatch(question.toCaseFolded());
    while (match.hasNext() && terms.size() < 8) {
        const QString term = match.next().captured();
        if (!ignored.contains(term) && !terms.contains(term))
            terms.append(term);
    }
    if (terms.isEmpty()) return {};
    // Exclude recent 48 messages: these are already represented by the
    // short-term conversation history sent to the LLM.
    QSqlQuery cutoff(m_db);
    cutoff.prepare("SELECT id FROM messages WHERE session_id=? "
                   "ORDER BY id DESC LIMIT 1 OFFSET 47");
    cutoff.addBindValue(session);
    if (!cutoff.exec() || !cutoff.next()) return {};
    QSqlQuery records(m_db);
    records.prepare("SELECT role,substr(body,1,1800) FROM messages "
                    "WHERE session_id=? AND id<? ORDER BY id DESC LIMIT 2048");
    records.addBindValue(session);
    records.addBindValue(cutoff.value(0).toLongLong());
    if (!records.exec()) return {};
    struct Hit {
        int score;
        int ordinal;
        QString role;
        QString text;
    };
    QList<Hit> matches;
    int ordinal = 0;
    while (records.next()) {
        const QString message = records.value(1).toString();
        const QString folded = message.toCaseFolded();
        int score = 0;
        qsizetype first = -1;
        for (const QString &term : terms) {
            const qsizetype found = folded.indexOf(term);
            if (found < 0) continue;
            score += term.size() >= 7 ? 3 : 2;
            if (first < 0 || found < first) first = found;
        }
        if (score && first >= 0) {
            const qsizetype begin = std::max<qsizetype>(0, first - 100);
            const QString excerpt = message.mid(begin, 380).simplified();
            if (!excerpt.isEmpty())
                matches.append(Hit{score, ordinal, records.value(0).toString(), excerpt});
        }
        ++ordinal;
    }
    std::sort(matches.begin(), matches.end(), [](const Hit &a, const Hit &b) {
        return a.score != b.score ? a.score > b.score : a.ordinal < b.ordinal;
    });
    QStringList excerpts;
    QSet<QString> duplicates;
    for (const Hit &hit : matches) {
        if (excerpts.size() >= std::clamp(maxResults, 1, 4)) break;
        if (duplicates.contains(hit.text)) continue;
        duplicates.insert(hit.text);
        excerpts.append(QString("[%1, earlier message] %2")
            .arg(hit.role == "you" ? "User" : "Ember", hit.text));
    }
    return excerpts.join('\n').left(1550);
}

bool SessionStore::exportMarkdown(const QString &session, const QString &path) const {
    if (!m_ready || !exists(session) || path.trimmed().isEmpty()) return false;
    // Stream the entire conversation, not just the last 300 UI messages.
    // QSaveFile commits atomically so cancelled/failed writes do not leave
    // a partial export. Nothing is sent to a remote service.
    QSqlQuery query(m_db);
    query.prepare("SELECT role,body FROM messages WHERE session_id=? ORDER BY id");
    query.addBindValue(session);
    if (!query.exec()) return false;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return false;
    QByteArray heading("# Ember conversation\n\n");
    const QString saved = notes(session);
    if (!saved.isEmpty())
        heading += QString("## Saved continuity notes\n\n%1\n\n").arg(saved).toUtf8();
    if (output.write(heading) != heading.size()) {
        output.cancelWriting();
        return false;
    }
    while (query.next()) {
        const QString speaker = query.value(0).toString() == "you" ? "You" : "Ember";
        const QByteArray row = QString("## %1\n\n%2\n\n")
            .arg(speaker, query.value(1).toString()).toUtf8();
        if (output.write(row) != row.size()) {
            output.cancelWriting();
            return false;
        }
    }
    return output.commit();
}
bool SessionStore::exists(const QString &session) const {
    if (!m_ready || session.isEmpty()) return false;
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM sessions WHERE id=? LIMIT 1");
    query.addBindValue(session);
    return query.exec() && query.next();
}
qint64 SessionStore::totalMessages(const QString &session) const {
    if (!m_ready || session.isEmpty()) return 0;
    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM messages WHERE session_id=?");
    query.addBindValue(session);
    return query.exec() && query.next() ? query.value(0).toLongLong() : 0;
}
QString SessionStore::notes(const QString &session) const {
    if (!m_ready || session.isEmpty()) return {};
    QSqlQuery query(m_db);
    query.prepare("SELECT body FROM session_notes WHERE session_id=? LIMIT 1");
    query.addBindValue(session);
    return query.exec() && query.next() ? query.value(0).toString() : QString();
}
QString SessionStore::asrHints(const QString &session) const {
    if (!m_ready || session.isEmpty()) return {};
    QSqlQuery query(m_db);
    query.prepare("SELECT body FROM session_asr_hints WHERE session_id=? LIMIT 1");
    query.addBindValue(session);
    return query.exec() && query.next() ? query.value(0).toString() : QString();
}
bool SessionStore::setAsrHints(const QString &session, const QString &hints) {
    if (!m_ready || !exists(session) || hints.size() > 512) return false;
    QSqlQuery query(m_db);
    if (hints.trimmed().isEmpty()) {
        query.prepare("DELETE FROM session_asr_hints WHERE session_id=?");
        query.addBindValue(session);
    } else {
        query.prepare("INSERT INTO session_asr_hints(session_id,body) VALUES(?,?) "
                      "ON CONFLICT(session_id) DO UPDATE SET body=excluded.body");
        query.addBindValue(session);
        query.addBindValue(hints.trimmed());
    }
    if (!query.exec()) {
        m_error = query.lastError().text();
        return false;
    }
    return true;
}
bool SessionStore::setNotes(const QString &session, const QString &text) {
    if (!m_ready || !exists(session) || text.size() > 1800) return false;
    QSqlQuery query(m_db);
    if (text.trimmed().isEmpty()) {
        query.prepare("DELETE FROM session_notes WHERE session_id=?");
        query.addBindValue(session);
    } else {
        query.prepare("INSERT INTO session_notes(session_id,body) VALUES(?,?) "
                      "ON CONFLICT(session_id) DO UPDATE SET body=excluded.body");
        query.addBindValue(session);
        query.addBindValue(text.trimmed());
    }
    if (!query.exec()) { m_error = query.lastError().text(); return false; }
    return true;
}
bool SessionStore::append(const QString &session, const QString &role,
                          const QString &body) {
    if (!m_ready || session.isEmpty() || (role != "you" && role != "ember") ||
        body.isEmpty() || body.size() > 32768) return false;
    if (!m_db.transaction()) return false;
    QSqlQuery insert(m_db);
    insert.prepare("INSERT INTO messages(session_id,role,body,created_at) VALUES(?,?,?,?)");
    insert.addBindValue(session);
    insert.addBindValue(role);
    insert.addBindValue(body);
    insert.addBindValue(nowUtc());
    if (!insert.exec()) { m_db.rollback(); m_error = insert.lastError().text(); return false; }
    QSqlQuery update(m_db);
    if (role == "you") {
        // A readable title from the first user message; no LLM call or upload.
        update.prepare("UPDATE sessions SET updated_at=?, "
                       "title=CASE WHEN (SELECT COUNT(*) FROM messages "
                       "WHERE session_id=? AND role='you')=1 "
                       "THEN ? ELSE title END WHERE id=?");
        update.addBindValue(nowUtc());
        update.addBindValue(session);
        QString title = body.simplified().left(74);
        if (body.simplified().size() > 74) title += "…";
        update.addBindValue(title);
        update.addBindValue(session);
    } else {
        update.prepare("UPDATE sessions SET updated_at=? WHERE id=?");
        update.addBindValue(nowUtc());
        update.addBindValue(session);
    }
    if (!update.exec()) { m_db.rollback(); m_error = update.lastError().text(); return false; }
    if (!m_db.commit()) { m_error = m_db.lastError().text(); return false; }
    return true;
}
bool SessionStore::remove(const QString &session) {
    if (!m_ready || session.isEmpty()) return false;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM sessions WHERE id=?");
    query.addBindValue(session);
    if (!query.exec()) { m_error = query.lastError().text(); return false; }
    return query.numRowsAffected() > 0;
}
