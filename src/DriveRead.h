#pragma once
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

namespace EmberDrive {
// Content reads require renewed explicit consent. Never reuse the old
// metadata-only refresh token after requesting a broader read-only scope.
constexpr auto kScope = "https://www.googleapis.com/auth/drive.readonly";
constexpr auto kCredential = "Ember.Local.GoogleDrive.ReadOnly";

bool validDesktopClientId(const QString &clientId);
// Validate a complete HTTP/1.1 loopback callback before exchanging its code.
// Never accept duplicate or spoofed Host headers, paths, states or codes.
bool validOAuthCallback(const QByteArray &headers, quint16 port,
                        const QByteArray &expectedState, QString *code = nullptr);
QByteArray randomPkceSecret();
QByteArray pkceChallenge(const QByteArray &verifier);
QUrl authorizationUrl(const QString &clientId, quint16 loopbackPort,
                      const QByteArray &verifier, const QByteArray &state);
QUrl folderListUrl(const QString &folderId);
QUrl fileMetadataUrl(const QString &fileId);
QUrl textContentUrl(const QString &fileId, const QString &mimeType);
struct TextPreview {
    QString text;
    QString error;
    int characters = 0;
    bool truncated = false;
    bool ok() const { return error.isEmpty(); }
};
TextPreview parseTextContent(const QString &fileId, const QString &fileName,
                             const QString &mimeType, const QByteArray &content);
bool validSecret(const QByteArray &token);
bool saveRefreshToken(const QByteArray &token,
    const QString &target = QString::fromLatin1(kCredential));
bool readRefreshToken(QByteArray &out,
    const QString &target = QString::fromLatin1(kCredential));
bool deleteRefreshToken(const QString &target =
    QString::fromLatin1(kCredential));
struct FolderPreview {
    QString text;
    QString error;
    int count = 0;
    bool ok() const { return error.isEmpty(); }
};
FolderPreview parseFolder(const QString &folderId, const QByteArray &json);
class Client final : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);
    ~Client() override;
    bool connectAccount(const QString &desktopClientId);
    bool listFolder(const QString &folderId, const QString &desktopClientId);
    // One selected Drive file, no search, polling, sync, edits or uploads.
    bool readTextFile(const QString &fileId, const QString &desktopClientId);
    void disconnectLocal();
signals:
    void status(const QString &text);
    void previewReady(const QString &text);
private:
    void acceptOAuthConnection();
    void exchangeAuthorizationCode(const QString &code);
    void requestRefresh();
    void requestFolder(const QString &folderId);
    void requestTextFile(const QString &fileId);
    void verifyAccount();
    void postToken(const QByteArray &body, bool refresh);
    enum class ResponseKind { Account, Folder, FileMetadata, FileText };
    void getDrive(const QUrl &url, ResponseKind kind,
                  const QString &id = {}, const QString &name = {},
                  const QString &mimeType = {});
    QTcpServer m_listener;
    QTimer m_authTimeout;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_request;
    QString m_clientId;
    QString m_previousClientId;
    QString m_redirect;
    QByteArray m_verifier, m_state, m_accessToken;
    qint64 m_accessExpires = 0;
    QString m_folderAfterRefresh;
    QString m_fileAfterRefresh;
};
}
