#include "SessionStore.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
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
             "CREATE TABLE IF NOT EXISTS sessions("
             "id TEXT PRIMARY KEY, title TEXT NOT NULL DEFAULT 'New conversation',"
             "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)",
             "CREATE TABLE IF NOT EXISTS messages("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,"
             "role TEXT NOT NULL CHECK(role IN ('you','ember')),"
             "body TEXT NOT NULL, created_at INTEGER NOT NULL)",
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
