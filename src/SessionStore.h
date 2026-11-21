#pragma once
#include <QSqlDatabase>
#include <QVariantList>
#include <QString>

// Small, private, local SQLite store. Only text: no microphone or TTS WAV data.
// One connection per instance, closed and removed on destruction.
class SessionStore final {
public:
    explicit SessionStore(const QString &path);
    ~SessionStore();
    SessionStore(const SessionStore &) = delete;
    SessionStore &operator=(const SessionStore &) = delete;
    bool ready() const { return m_ready; }
    QString error() const { return m_error; }
    QString createSession();
    QVariantList sessions(int limit = 80) const;
    QVariantList search(const QString &text, int limit = 50) const;
    bool exportMarkdown(const QString &session, const QString &path) const;
    QVariantList messages(const QString &session, int limit = 300) const;
    bool append(const QString &session, const QString &role, const QString &body);
    bool remove(const QString &session);
    bool exists(const QString &session) const;
    qint64 totalMessages(const QString &session) const;
private:
    bool execute(const QString &statement);
    QString m_connectionName;
    QSqlDatabase m_db;
    QString m_error;
    bool m_ready = false;
};
