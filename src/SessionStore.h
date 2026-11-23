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
    QVariantList sessions(int limit = 80, int offset = 0) const;
    QVariantList search(const QString &text, int limit = 50) const;
    // Small, opt-in-to-this-chat lexical recollection; no embeddings or cloud.
    QString historicalExcerpts(const QString &session, const QString &question,
                               int maxResults = 3) const;
    // Foreground lexical recall from OTHER saved chats. Caller MUST check
    // an explicit opt-in and private-mode boundary; no index or background task.
    QString relatedSavedExcerpts(const QString &currentSession,
                                 const QString &question,
                                 int maxResults = 2) const;
    bool exportMarkdown(const QString &session, const QString &path) const;
    // Explicit local-only compressed backup. Not encrypted or cloud synced.
    bool exportCompactBackup(const QString &path) const;
    // Transactional restore into new sessions: never replaces existing chats.
    bool restoreCompactBackup(const QString &path);
    QVariantList messages(const QString &session, int limit = 300,
                          int offset = 0) const;
    // Foreground-only stable SQLite ID windows; never materialize a whole
    // multi-year conversation just to navigate to an archived search hit.
    QVariantList messagesAround(const QString &session, qint64 messageId,
                                int before = 40, int after = 40) const;
    QVariantList messagesBefore(const QString &session, qint64 messageId,
                                int limit = 100) const;
    QVariantList messagesAfter(const QString &session, qint64 messageId,
                               int limit = 100) const;
    bool hasMessageBefore(const QString &session, qint64 messageId) const;
    bool hasMessageAfter(const QString &session, qint64 messageId) const;
    qint64 latestMessageId(const QString &session) const;
    bool append(const QString &session, const QString &role, const QString &body);
    bool remove(const QString &session);
    bool exists(const QString &session) const;
    qint64 totalMessages(const QString &session) const;
    QString notes(const QString &session) const;
    QString asrHints(const QString &session) const;
    bool setAsrHints(const QString &session, const QString &hints);
    bool setNotes(const QString &session, const QString &notes);
private:
    bool execute(const QString &statement);
    QString m_connectionName;
    QSqlDatabase m_db;
    QString m_error;
    bool m_ready = false;
};
