#include "SessionStore.h"
#include "Core.h"
#include <QDateTime>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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


// Center the visible archive preview around the actual lexical match.
// Old chats frequently contain long stories/logs: showing the first 160
// characters when the match is thousands of characters later is misleading.
// This operates only on the already bounded current search result, without
// an index, background task, cloud call or extra retained transcript.
QString matchingSearchSnippet(const QString &body, const QString &needle) {
    if (body.isEmpty()) return {};
    const qsizetype hit = body.indexOf(needle, 0, Qt::CaseInsensitive);
    qsizetype begin = hit < 0 ? 0 : std::max<qsizetype>(0, hit - 64);
    qsizetype end = hit < 0
        ? std::min<qsizetype>(body.size(), 180)
        : std::min<qsizetype>(body.size(),
            std::max<qsizetype>(begin + 180, hit + needle.size() + 64));
    // QString positions count UTF-16 code units. Do not cut an emoji or
    // another supplementary Unicode character in half at excerpt edges.
    if (begin > 0 && body[begin].isLowSurrogate() &&
        body[begin - 1].isHighSurrogate()) --begin;
    if (end < body.size() && end > 0 && body[end - 1].isHighSurrogate() &&
        body[end].isLowSurrogate()) ++end;
    QString preview = body.mid(begin, end - begin).simplified();
    if (begin > 0) preview.prepend(QChar(0x2026));
    if (end < body.size()) preview.append(QChar(0x2026));
    return preview;
}

// A lexical hit without the surrounding turn can invert the meaning of an
// older correction, instruction or story exchange. Recover at most ONE
// neighboring turn from the same persisted session. The indexed (session_id,id)
// lookup runs only for selected hits, not for every scanned archive message.
QString pairedRecallExcerpt(const QSqlDatabase &db, const QString &session,
                            qint64 messageId, const QString &role,
                            const QString &matchedText, qint64 *neighborId) {
    if (neighborId) *neighborId = 0;
    const bool userHit = role == QLatin1String("you");
    const QString own = QString(userHit ? "[User excerpt] " : "[Ember excerpt] ")
        + matchedText;
    if (messageId <= 0 || (role != QLatin1String("you") &&
                           role != QLatin1String("ember"))) return own;
    QSqlQuery query(db);
    query.prepare(userHit
        ? "SELECT id,role,substr(body,1,900) FROM messages "
          "WHERE session_id=? AND id>? ORDER BY id ASC LIMIT 1"
        : "SELECT id,role,substr(body,1,900) FROM messages "
          "WHERE session_id=? AND id<? ORDER BY id DESC LIMIT 1");
    query.addBindValue(session);
    query.addBindValue(messageId);
    if (!query.exec() || !query.next() ||
        query.value(1).toString() != (userHit ? QLatin1String("ember")
                                             : QLatin1String("you"))) return own;
    const QString excerpt = query.value(2).toString().simplified().left(190);
    if (excerpt.isEmpty()) return own;
    if (neighborId) *neighborId = query.value(0).toLongLong();
    // Always show chronological user -> assistant context, even when the
    // lexical match came from Ember's reply. These are unverified excerpts,
    // never a new request or a claim that the complete turn was retained.
    return userHit
        ? own + "\n[Following Ember reply excerpt] " + excerpt
        : QString("[Preceding user request excerpt] ") + excerpt + "\n" + own;
}
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
QVariantList SessionStore::sessions(int limit, int offset) const {
    QVariantList result;
    if (!m_ready) return result;
    QSqlQuery query(m_db);
    if (offset < 0) return result;
    query.prepare("SELECT id,title,updated_at FROM sessions "
                  "ORDER BY updated_at DESC, rowid DESC LIMIT ? OFFSET ?");
    query.addBindValue(std::clamp(limit, 1, 200));
    query.addBindValue(offset);
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
QVariantList SessionStore::messages(const QString &session,
                                    int limit, int offset) const {
    QVariantList reverse;
    if (!m_ready || session.isEmpty() || offset < 0) return reverse;
    QSqlQuery query(m_db);
    query.prepare("SELECT id,role,body FROM messages WHERE session_id=? "
                  "ORDER BY id DESC LIMIT ? OFFSET ?");
    query.addBindValue(session);
    query.addBindValue(std::clamp(limit, 1, 500));
    query.addBindValue(offset);
    if (!query.exec()) return reverse;
    while (query.next()) {
        reverse.append(QVariantMap{{"id", query.value(0).toLongLong()},
                                   {"role", query.value(1).toString()},
                                   {"body", query.value(2).toString()}});
    }
    std::reverse(reverse.begin(), reverse.end());
    return reverse;
}
QVariantList SessionStore::messagesBefore(const QString &session,
                                          qint64 messageId, int limit) const {
    QVariantList reverse;
    if (!m_ready || session.isEmpty() || messageId <= 0) return reverse;
    QSqlQuery query(m_db);
    query.prepare("SELECT id,role,body FROM messages "
                  "WHERE session_id=? AND id<? ORDER BY id DESC LIMIT ?");
    query.addBindValue(session);
    query.addBindValue(messageId);
    query.addBindValue(std::clamp(limit, 1, 100));
    if (!query.exec()) return {};
    while (query.next()) {
        reverse.append(QVariantMap{{"id", query.value(0).toLongLong()},
                                   {"role", query.value(1).toString()},
                                   {"body", query.value(2).toString()}});
    }
    std::reverse(reverse.begin(), reverse.end());
    return reverse;
}
QVariantList SessionStore::messagesAfter(const QString &session,
                                         qint64 messageId, int limit) const {
    QVariantList result;
    if (!m_ready || session.isEmpty() || messageId <= 0) return result;
    QSqlQuery query(m_db);
    query.prepare("SELECT id,role,body FROM messages "
                  "WHERE session_id=? AND id>? ORDER BY id ASC LIMIT ?");
    query.addBindValue(session);
    query.addBindValue(messageId);
    query.addBindValue(std::clamp(limit, 1, 100));
    if (!query.exec()) return {};
    while (query.next()) {
        result.append(QVariantMap{{"id", query.value(0).toLongLong()},
                                  {"role", query.value(1).toString()},
                                  {"body", query.value(2).toString()}});
    }
    return result;
}
QVariantList SessionStore::messagesAround(const QString &session,
                                          qint64 messageId,
                                          int before, int after) const {
    if (!m_ready || session.isEmpty() || messageId <= 0) return {};
    // Validate session membership before reading either neighbor range.
    QSqlQuery check(m_db);
    check.prepare("SELECT role,body FROM messages WHERE session_id=? AND id=?");
    check.addBindValue(session);
    check.addBindValue(messageId);
    if (!check.exec() || !check.next()) return {};
    const QVariantMap hit{{"id", messageId},
                          {"role", check.value(0).toString()},
                          {"body", check.value(1).toString()}};
    QVariantList window;
    if (before > 0)
        window = messagesBefore(session, messageId, std::clamp(before, 1, 100));
    window.append(hit);
    if (after > 0)
        window.append(messagesAfter(session, messageId, std::clamp(after, 1, 100)));
    return window;
}
bool SessionStore::hasMessageBefore(const QString &session,
                                    qint64 messageId) const {
    if (!m_ready || session.isEmpty() || messageId <= 0) return false;
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM messages WHERE session_id=? AND id<? LIMIT 1");
    query.addBindValue(session);
    query.addBindValue(messageId);
    return query.exec() && query.next();
}
bool SessionStore::hasMessageAfter(const QString &session,
                                   qint64 messageId) const {
    if (!m_ready || session.isEmpty() || messageId <= 0) return false;
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM messages WHERE session_id=? AND id>? LIMIT 1");
    query.addBindValue(session);
    query.addBindValue(messageId);
    return query.exec() && query.next();
}
qint64 SessionStore::latestMessageId(const QString &session) const {
    if (!m_ready || session.isEmpty()) return 0;
    QSqlQuery query(m_db);
    query.prepare("SELECT id FROM messages WHERE session_id=? "
                  "ORDER BY id DESC LIMIT 1");
    query.addBindValue(session);
    return query.exec() && query.next() ? query.value(0).toLongLong() : 0;
}
QVariantList SessionStore::search(const QString &text, int limit) const {
    QVariantList result;
    const QString term = text.trimmed();
    if (!m_ready || term.isEmpty() || term.size() > 120) return result;
    // SQLite's built-in LIKE normally folds ASCII only. Include bounded
    // lowercase/uppercase/initial-capital variants for ordinary RU/EN names
    // without creating a second persistent FTS index or touching other chats.
    QStringList forms{term};
    const bool nonAsciiLetters = std::any_of(term.cbegin(), term.cend(),
        [](QChar ch) { return ch.unicode() > 127 && ch.isLetter(); });
    if (nonAsciiLetters) {
        const QString lower = term.toLower();
        const QString upper = term.toUpper();
        const QString capitalized = lower.left(1).toUpper() + lower.mid(1);
        for (const QString &form : {lower, upper, capitalized})
            if (!forms.contains(form)) forms.append(form);
    }
    // Quote every LIKE wildcard and escape character. Searching for "100%"
    // must remain literal even when several case variants are bound.
    QStringList patterns;
    for (QString form : forms) {
        form.replace("\\", "\\\\");
        form.replace("%", "\\%");
        form.replace("_", "\\_");
        patterns.append("%" + form + "%");
    }
    const auto matchClause = [&](const QString &column) {
        QStringList tests;
        for (qsizetype i = 0; i < patterns.size(); ++i)
            tests.append(column + " LIKE ? ESCAPE '\\'");
        return "(" + tests.join(" OR ") + ")";
    };
    // Select the matching message ID and its text from the SAME row. A
    // title-only match has messageId=0 and opens the regular latest view.
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT s.id, s.title, s.updated_at,"
        "COALESCE(substr(m.body,1,65664),''),COALESCE(m.id,0) "
        "FROM sessions s LEFT JOIN messages m ON m.id=("
        "SELECT f.id FROM messages f WHERE f.session_id=s.id AND " +
        matchClause("f.body") + " ORDER BY f.id DESC LIMIT 1) "
        "WHERE " + matchClause("s.title") +
        " OR m.id IS NOT NULL "
        "ORDER BY s.updated_at DESC, s.rowid DESC LIMIT ?");
    for (int occurrence = 0; occurrence < 2; ++occurrence)
        for (const QString &pattern : patterns) query.addBindValue(pattern);
    query.addBindValue(std::clamp(limit, 1, 100));
    if (!query.exec()) return result;
    while (query.next()) {
        result.append(QVariantMap{
            {"id", query.value(0).toString()},
            {"title", query.value(1).toString()},
            {"updated", query.value(2).toLongLong()},
            {"snippet", matchingSearchSnippet(query.value(3).toString(), term)},
            {"messageId", QString::number(query.value(4).toLongLong())}
        });
    }
    return result;
}
QString SessionStore::relatedSavedExcerpts(const QString &currentSession,
                                           const QString &question,
                                           int maxResults) const {
    // This operation is deliberately foreground-only. A separate, initially
    // disabled setting in EmberBackend must authorize cross-chat recall.
    if (!m_ready || currentSession.isEmpty() || currentSession.size() > 64 ||
        !exists(currentSession) || question.trimmed().isEmpty() ||
        question.size() > 8192) return {};
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
    auto matches = words.globalMatch(question.toCaseFolded());
    while (matches.hasNext() && terms.size() < 8) {
        const QString word = matches.next().captured();
        if (!ignored.contains(word) && !terms.contains(word))
            terms.append(word);
    }
    if (terms.isEmpty()) return {};
    // Bounded scan: no persistent duplicate index, no embeddings, no GPU
    // and no attempt to search non-chat files or connected accounts.
    QSqlQuery query(m_db);
    query.prepare("SELECT s.title,m.role,substr(m.body,1,1800),m.id,m.session_id "
                  "FROM messages m JOIN sessions s ON s.id=m.session_id "
                  "WHERE m.session_id<>? ORDER BY m.id DESC LIMIT 1600");
    query.addBindValue(currentSession);
    if (!query.exec()) return {};
    struct Hit { int score; int ordinal; qint64 id; QString session, title, role, text; };
    QList<Hit> hits;
    int ordinal = 0;
    while (query.next()) {
        const QString body = query.value(2).toString();
        const QString folded = body.toCaseFolded();
        int score = 0;
        qsizetype first = -1;
        for (const QString &term : terms) {
            const qsizetype found = folded.indexOf(term);
            if (found < 0) continue;
            score += term.size() >= 7 ? 3 : 2;
            if (first < 0 || found < first) first = found;
        }
        if (score > 0 && first >= 0) {
            const qsizetype begin = std::max<qsizetype>(0, first - 100);
            const QString excerpt = body.mid(begin, 285).simplified();
            if (!excerpt.isEmpty())
                hits.append(Hit{score, ordinal, query.value(3).toLongLong(),
                    query.value(4).toString(),
                    query.value(0).toString().simplified().left(72),
                    query.value(1).toString(), excerpt});
        }
        ++ordinal;
    }
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        return a.score != b.score ? a.score > b.score : a.ordinal < b.ordinal;
    });
    QSet<QString> seen;
    QSet<QString> seenPairs;
    QStringList excerpts;
    for (const Hit &hit : hits) {
        if (excerpts.size() >= std::clamp(maxResults, 1, 3)) break;
        if (seen.contains(hit.text)) continue;
        qint64 neighborId = 0;
        const QString paired = pairedRecallExcerpt(
            m_db, hit.session, hit.id, hit.role, hit.text, &neighborId);
        const QString pairKey = hit.session + QLatin1Char(':') +
            QString::number(neighborId > 0
                ? std::min(hit.id, neighborId) : hit.id);
        if (seenPairs.contains(pairKey)) continue;
        seenPairs.insert(pairKey);
        seen.insert(hit.text);
        excerpts.append(QString("[Saved chat: %1]\n%2")
                            .arg(hit.title, paired));
    }
    return excerpts.join('\n');
}

QString SessionStore::historicalExcerpts(const QString &session,
                                         const QString &question,
                                         int maxResults) const {
    if (!m_ready || session.isEmpty() || session.size() > 64 ||
        question.trimmed().isEmpty() || question.size() > 8192) return {};
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
    records.prepare("SELECT id,role,substr(body,1,1800) FROM messages "
                    "WHERE session_id=? AND id<? ORDER BY id DESC LIMIT 2048");
    records.addBindValue(session);
    records.addBindValue(cutoff.value(0).toLongLong());
    if (!records.exec()) return {};
    struct Hit {
        int score;
        int ordinal;
        qint64 id;
        QString role;
        QString text;
    };
    QList<Hit> matches;
    int ordinal = 0;
    while (records.next()) {
        const QString message = records.value(2).toString();
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
                matches.append(Hit{score, ordinal, records.value(0).toLongLong(),
                                        records.value(1).toString(), excerpt});
        }
        ++ordinal;
    }
    std::sort(matches.begin(), matches.end(), [](const Hit &a, const Hit &b) {
        return a.score != b.score ? a.score > b.score : a.ordinal < b.ordinal;
    });
    QStringList excerpts;
    QSet<QString> duplicates;
    QSet<qint64> seenPairs;
    for (const Hit &hit : matches) {
        if (excerpts.size() >= std::clamp(maxResults, 1, 4)) break;
        if (duplicates.contains(hit.text)) continue;
        qint64 neighborId = 0;
        const QString paired = pairedRecallExcerpt(
            m_db, session, hit.id, hit.role, hit.text, &neighborId);
        const qint64 pairKey = neighborId > 0
            ? std::min(hit.id, neighborId) : hit.id;
        if (seenPairs.contains(pairKey)) continue;
        seenPairs.insert(pairKey);
        duplicates.insert(hit.text);
        excerpts.append(QString("[Earlier saved conversation]\n%1")
            .arg(paired));
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

bool SessionStore::exportCompactBackup(const QString &path) const {
    if (!m_ready || path.trimmed().isEmpty() ||
        QFileInfo(path).absoluteFilePath() ==
            QFileInfo(m_db.databaseName()).absoluteFilePath()) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    static const QByteArray magic = QByteArrayLiteral("EMBER-CHAT-BACKUP-1\n");
    if (file.write(magic) != magic.size()) {
        file.cancelWriting();
        return false;
    }
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_5);
    // Each completed text response may be 65,536 UTF-16 characters.
    // Its UTF-8 JSON representation (including escaping) can exceed 192 KiB.
    // Bound both compressed and expanded frames to 512 KiB and keep the
    // pre-decompression size check for imported, untrusted backup files.
    const auto frame = [&](const QJsonObject &obj) -> bool {
        const QByteArray raw = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        if (raw.isEmpty() || raw.size() > 512 * 1024) return false;
        const QByteArray packed = qCompress(raw, 9);
        if (packed.size() < 5 || packed.size() > 512 * 1024) return false;
        stream << quint32(packed.size());
        return stream.status() == QDataStream::Ok &&
            stream.writeRawData(packed.constData(), int(packed.size())) ==
                packed.size() && stream.status() == QDataStream::Ok;
    };
    QSqlQuery sessionsQuery(m_db);
    bool ok = sessionsQuery.exec(
        "SELECT id,title,created_at,updated_at FROM sessions ORDER BY created_at,id");
    int sessionCount = 0;
    qint64 messageCount = 0;
    while (ok && sessionsQuery.next()) {
        if (++sessionCount > 10000) { ok = false; break; }
        const QString id = sessionsQuery.value(0).toString();
        ok = frame(QJsonObject{{"kind", "session"},
              {"title", sessionsQuery.value(1).toString()},
              {"created", double(sessionsQuery.value(2).toLongLong())},
              {"updated", double(sessionsQuery.value(3).toLongLong())},
              {"notes", notes(id)}, {"hints", asrHints(id)}});
        if (!ok) break;
        QSqlQuery messagesQuery(m_db);
        messagesQuery.prepare(
            "SELECT role,body,created_at FROM messages WHERE session_id=? ORDER BY id");
        messagesQuery.addBindValue(id);
        ok = messagesQuery.exec();
        while (ok && messagesQuery.next()) {
            if (++messageCount > 1000000) { ok = false; break; }
            ok = frame(QJsonObject{{"kind", "message"},
                  {"role", messagesQuery.value(0).toString()},
                  {"body", messagesQuery.value(1).toString()},
                  {"created", double(messagesQuery.value(2).toLongLong())}});
        }
    }
    if (ok) {
        stream << quint32(0); // mandatory final marker; detect truncation
        ok = stream.status() == QDataStream::Ok;
    }
    if (!ok) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool SessionStore::restoreCompactBackup(const QString &path) {
    if (!m_ready || path.trimmed().isEmpty() ||
        QFileInfo(path).absoluteFilePath() ==
            QFileInfo(m_db.databaseName()).absoluteFilePath()) return false;
    QFile file(path);
    static const QByteArray magic = QByteArrayLiteral("EMBER-CHAT-BACKUP-1\n");
    if (!file.open(QIODevice::ReadOnly) || file.read(magic.size()) != magic)
        return false;
    if (!m_db.transaction()) return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_5);
    const auto fail = [&]() -> bool {
        m_db.rollback();
        m_error = "Malformed, truncated or unsupported Ember backup; no chats restored.";
        return false;
    };
    QString currentSession;
    int sessionsRead = 0;
    qint64 messagesRead = 0;
    bool ended = false;
    while (!file.atEnd()) {
        quint32 length = 0;
        stream >> length;
        if (stream.status() != QDataStream::Ok) return fail();
        if (length == 0) {
            ended = file.atEnd();
            break;
        }
        if (length < 5 || length > 512 * 1024) return fail();
        QByteArray packed(int(length), char(0));
        if (stream.readRawData(packed.data(), int(length)) != int(length))
            return fail();
        // qUncompress uses an untrusted four-byte decoded-size header.
        // Check it before decompression to prevent oversized allocation.
        const auto *p = reinterpret_cast<const uchar *>(packed.constData());
        const quint32 expanded = (quint32(p[0]) << 24) |
            (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
        if (expanded == 0 || expanded > 512 * 1024) return fail();
        const QByteArray raw = qUncompress(packed);
        if (raw.size() != expanded) return fail();
        QJsonParseError parseError;
        const QJsonDocument decoded = QJsonDocument::fromJson(raw, &parseError);
        if (parseError.error != QJsonParseError::NoError || !decoded.isObject())
            return fail();
        const QJsonObject obj = decoded.object();
        const double epoch = obj.value("created").toDouble(-1);
        if (epoch < 0 || epoch > 4102444800.0) return fail();
        if (obj.value("kind") == QLatin1String("session")) {
            if (++sessionsRead > 10000) return fail();
            const QString title = obj.value("title").toString();
            const QString notesText = obj.value("notes").toString();
            const QString hintsText = obj.value("hints").toString();
            const double updated = obj.value("updated").toDouble(-1);
            if (title.isEmpty() || title.size() > 256 ||
                notesText.size() > 1800 || hintsText.size() > 512 ||
                updated < 0 || updated > 4102444800.0) return fail();
            currentSession = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QSqlQuery insert(m_db);
            insert.prepare(
                "INSERT INTO sessions(id,title,created_at,updated_at) VALUES(?,?,?,?)");
            insert.addBindValue(currentSession);
            insert.addBindValue(title);
            insert.addBindValue(qint64(epoch));
            insert.addBindValue(qint64(updated));
            if (!insert.exec() ||
                (!notesText.isEmpty() && !setNotes(currentSession, notesText)) ||
                (!hintsText.isEmpty() && !setAsrHints(currentSession, hintsText)))
                return fail();
        } else if (obj.value("kind") == QLatin1String("message")) {
            if (currentSession.isEmpty() || ++messagesRead > 1000000)
                return fail();
            const QString role = obj.value("role").toString();
            const QString body = obj.value("body").toString();
            if ((role != "you" && role != "ember") ||
                body.isEmpty() || body.size() > EmberCore::kMaxArchivedChatCharacters) return fail();
            QSqlQuery insert(m_db);
            insert.prepare(
                "INSERT INTO messages(session_id,role,body,created_at) VALUES(?,?,?,?)");
            insert.addBindValue(currentSession);
            insert.addBindValue(role);
            insert.addBindValue(body);
            insert.addBindValue(qint64(epoch));
            if (!insert.exec()) return fail();
        } else {
            return fail();
        }
    }
    if (!ended) return fail(); // missing final frame or trailing data
    if (!m_db.commit()) return fail();
    return true;
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
        body.isEmpty() || body.size() > EmberCore::kMaxArchivedChatCharacters) return false;
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
