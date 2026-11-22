#include "DriveRead.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUrlQuery>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace EmberDrive {
namespace {
constexpr qsizetype kMaximumResultBytes = 128 * 1024;
QString clean(QString value, int limit = 180) {
    for (QChar &c : value) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F ||
            c.unicode() == 0x2028 || c.unicode() == 0x2029)
            c = QLatin1Char(' ');
    }
    return value.simplified().left(limit);
}
bool allowedTarget(const QString &target) {
    if (target == QString::fromLatin1(kCredential)) return true;
    static const QRegularExpression test(QStringLiteral(
        "^Ember\\.Local\\.GoogleDrive\\.Test\\.[A-Za-z0-9-]{1,55}$"));
    return test.match(target).hasMatch();
}
bool folderIdOkay(const QString &id) {
    static const QRegularExpression idRx(QStringLiteral("^[A-Za-z0-9_-]{8,160}$"));
    return id == "root" || idRx.match(id).hasMatch();
}
QByteArray postForm(const QString &clientId, const QString &grantType,
                    const QString &code, const QByteArray &verifier,
                    const QString &redirect) {
    QUrlQuery q;
    q.addQueryItem("client_id", clientId);
    q.addQueryItem("grant_type", grantType);
    if (grantType == "authorization_code") {
        q.addQueryItem("code", code);
        q.addQueryItem("code_verifier", QString::fromLatin1(verifier));
        q.addQueryItem("redirect_uri", redirect);
    } else {
        q.addQueryItem("refresh_token", code);
    }
    return q.toString(QUrl::FullyEncoded).toUtf8();
}
}
bool validDesktopClientId(const QString &clientId) {
    static const QRegularExpression rx(QStringLiteral(
        "^[A-Za-z0-9-]{10,170}\\.apps\\.googleusercontent\\.com$"));
    return rx.match(clientId).hasMatch();
}
bool validOAuthCallback(const QByteArray &headers, quint16 port,
                        const QByteArray &expectedState, QString *code) {
    if (code) code->clear();
    if (!port || expectedState.isEmpty() || headers.isEmpty() ||
        headers.size() > 4096 || !headers.endsWith("\r\n\r\n"))
        return false;
    const auto lines = headers.left(headers.size() - 4).split('\n');
    if (lines.size() < 2) return false;
    for (const auto &line : lines)
        if (!line.endsWith('\r')) return false;
    QByteArray requestLine = lines.first();
    requestLine.chop(1);
    const auto parts = requestLine.split(' ');
    if (parts.size() != 3 || parts.at(0) != "GET" ||
        parts.at(2) != "HTTP/1.1" || !parts.at(1).startsWith("/?") ||
        parts.at(1).contains('#') || parts.at(1).contains('\\'))
        return false;
    const QByteArray expectedHost =
        "127.0.0.1:" + QByteArray::number(port);
    int hostCount = 0;
    for (int i = 1; i < lines.size(); ++i) {
        QByteArray line = lines.at(i);
        line.chop(1);
        const qsizetype colon = line.indexOf(':');
        if (colon < 1) return false;
        if (line.left(colon).trimmed().toLower() == "host") {
            ++hostCount;
            if (line.mid(colon + 1).trimmed() != expectedHost)
                return false;
        }
    }
    if (hostCount != 1) return false;
    const QUrl callback = QUrl::fromEncoded(
        "http://127.0.0.1" + parts.at(1), QUrl::StrictMode);
    if (!callback.isValid() || callback.path() != "/" ||
        callback.host() != "127.0.0.1" || callback.port(-1) != -1 ||
        !callback.fragment().isEmpty() || !callback.userInfo().isEmpty())
        return false;
    const QUrlQuery query(callback);
    int states = 0, codes = 0;
    for (const auto &item : query.queryItems()) {
        if (item.first == "state") ++states;
        if (item.first == "code") ++codes;
    }
    if (states != 1 || codes != 1 ||
        query.queryItemValue("state") != QString::fromLatin1(expectedState))
        return false;
    const QString result = query.queryItemValue("code");
    if (result.size() < 8 || result.size() > 2048)
        return false;
    for (const QChar c : result)
        if (c.unicode() < 0x20 || c.unicode() == 0x7F)
            return false;
    if (code) *code = result;
    return true;
}
bool validSecret(const QByteArray &value) {
    if (value.size() < 16 || value.size() > 2048) return false;
    for (const char c : value) {
        const unsigned char x = static_cast<unsigned char>(c);
        if (!((x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') ||
              (x >= '0' && x <= '9') || x == '.' || x == '_' ||
              x == '-' || x == '/' || x == '~')) return false;
    }
    return true;
}
QByteArray randomPkceSecret() {
    QByteArray bytes(32, '\0');
    auto *generator = QRandomGenerator::system();
    for (int i = 0; i < 32; i += 4) {
        const quint32 value = generator->generate();
        for (int b = 0; b < 4; ++b)
            bytes[i + b] = char((value >> (b * 8)) & 0xff);
    }
    const QByteArray result = bytes.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    bytes.fill('\0');
    return result;
}
QByteArray pkceChallenge(const QByteArray &verifier) {
    return QCryptographicHash::hash(
        verifier, QCryptographicHash::Sha256).toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QUrl authorizationUrl(const QString &clientId, quint16 port,
                      const QByteArray &verifier, const QByteArray &state) {
    if (!validDesktopClientId(clientId) || port == 0 ||
        verifier.size() < 43 || verifier.size() > 128 ||
        state.size() < 32 || state.size() > 128) return {};
    for (const QByteArray &part : {verifier, state}) {
        for (char c : part) {
            if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
                !(c >= '0' && c <= '9') && c != '-' && c != '_')
                return {};
        }
    }
    QUrl url("https://accounts.google.com/o/oauth2/v2/auth");
    QUrlQuery q;
    q.addQueryItem("client_id", clientId);
    q.addQueryItem("response_type", "code");
    q.addQueryItem("redirect_uri", QString("http://127.0.0.1:%1").arg(port));
    q.addQueryItem("scope", QString::fromLatin1(kScope));
    q.addQueryItem("access_type", "offline");
    q.addQueryItem("prompt", "consent select_account");
    q.addQueryItem("state", QString::fromLatin1(state));
    q.addQueryItem("code_challenge", QString::fromLatin1(pkceChallenge(verifier)));
    q.addQueryItem("code_challenge_method", "S256");
    url.setQuery(q);
    return url;
}
QUrl folderListUrl(const QString &folderId) {
    if (!folderIdOkay(folderId)) return {};
    QUrl url("https://www.googleapis.com/drive/v3/files");
    QUrlQuery q;
    q.addQueryItem("q", QString("'%1' in parents and trashed = false").arg(folderId));
    q.addQueryItem("fields",
        "nextPageToken,files(id,name,mimeType,modifiedTime)");
    q.addQueryItem("pageSize", "20");
    q.addQueryItem("orderBy", "folder,name");
    url.setQuery(q);
    return url;
}
bool saveRefreshToken(const QByteArray &token, const QString &target) {
    if (!allowedTarget(target) || !validSecret(token)) return false;
#ifdef Q_OS_WIN
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(
        reinterpret_cast<LPCWSTR>(target.utf16()));
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(
        const_cast<char *>(token.constData()));
    credential.CredentialBlobSize = DWORD(token.size());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"Google Drive metadata-only OAuth");
    return CredWriteW(&credential, 0) != 0;
#else
    return false;
#endif
}
bool readRefreshToken(QByteArray &out, const QString &target) {
    out.clear();
    if (!allowedTarget(target)) return false;
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()),
                   CRED_TYPE_GENERIC, 0, &credential)) return false;
    QByteArray bytes;
    if (credential && credential->CredentialBlob &&
        credential->CredentialBlobSize <= 2048) {
        bytes = QByteArray(
            reinterpret_cast<const char *>(credential->CredentialBlob),
            qsizetype(credential->CredentialBlobSize));
    }
    if (credential) CredFree(credential);
    const bool valid = validSecret(bytes);
    if (valid) out = bytes;
    bytes.fill('\0');
    return valid;
#else
    return false;
#endif
}
bool deleteRefreshToken(const QString &target) {
    if (!allowedTarget(target)) return false;
#ifdef Q_OS_WIN
    if (CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()),
                    CRED_TYPE_GENERIC, 0)) return true;
    return GetLastError() == ERROR_NOT_FOUND;
#else
    return false;
#endif
}
FolderPreview parseFolder(const QString &folderId, const QByteArray &data) {
    if (!folderIdOkay(folderId))
        return {{}, "Invalid Google Drive folder ID.", 0};
    if (data.isEmpty() || data.size() > kMaximumResultBytes)
        return {{}, "Google Drive response is empty or too large.", 0};
    QJsonParseError error{};
    const QJsonDocument json = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject() ||
        !json.object().value("files").isArray())
        return {{}, "Google Drive returned an invalid file listing.", 0};
    FolderPreview result;
    result.text = "\n\n---\nGoogle Drive read-only metadata for chosen folder " +
        folderId + "\nUntrusted names and source links, not file contents:\n";
    const auto files = json.object().value("files").toArray();
    for (const auto &item : files) {
        if (!item.isObject()) continue;
        const auto obj = item.toObject();
        const QString id = obj.value("id").toString();
        if (!folderIdOkay(id)) continue;
        const QString name = clean(obj.value("name").toString(), 150);
        const QString type = clean(obj.value("mimeType").toString(), 85);
        const QString modified = clean(obj.value("modifiedTime").toString(), 45);
        const QString line = QString("\n%1. %2\nType: %3 | Modified: %4\n"
                                     "Open: https://drive.google.com/open?id=%5\n")
            .arg(result.count + 1).arg(name, type, modified, id);
        if (result.text.size() + line.size() > 6500) break;
        result.text += line;
        if (++result.count == 20) break;
    }
    if (result.count == 0) result.text += "\nNo files found on this page.\n";
    if (!json.object().value("nextPageToken").toString().isEmpty())
        result.text += "\nMore entries exist; only the first 20 are shown.\n";
    result.text += "--- End Google Drive metadata ---";
    return result;
}
Client::Client(QObject *parent) : QObject(parent) {
    m_network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    connect(&m_listener, &QTcpServer::newConnection,
            this, &Client::acceptOAuthConnection);
    m_authTimeout.setSingleShot(true);
    connect(&m_authTimeout, &QTimer::timeout, this, [this]() {
        m_listener.close();
        m_verifier.fill('\0');
        m_verifier.clear();
        m_state.fill('\0');
        m_state.clear();
        m_clientId = m_previousClientId;
        m_previousClientId.clear();
        emit status("Google consent timed out. Previous account credentials remain unchanged.");
    });
}
Client::~Client() {
    if (m_request) m_request->abort();
    m_listener.close();
    m_authTimeout.stop();
    m_accessToken.fill('\0');
    m_verifier.fill('\0');
    m_state.fill('\0');
}
bool Client::connectAccount(const QString &clientId) {
    if (m_request || m_listener.isListening()) {
        emit status("Finish the active Google authorization first.");
        return false;
    }
    if (!validDesktopClientId(clientId)) {
        emit status("A Google Cloud Desktop OAuth client ID is required.");
        return false;
    }
    if (!m_listener.listen(QHostAddress::LocalHost, 0)) {
        emit status("Could not listen on IPv4 loopback for Google consent.");
        return false;
    }
    // Keep the existing credential until the new OAuth exchange succeeds:
    // cancel, browser failure, timeout and token errors must not log out
    // the previously connected account.
    m_previousClientId = m_clientId;
    m_clientId = clientId;
    m_verifier = randomPkceSecret();
    m_state = randomPkceSecret();
    m_redirect = QString("http://127.0.0.1:%1").arg(m_listener.serverPort());
    const QUrl auth = authorizationUrl(clientId, m_listener.serverPort(),
                                       m_verifier, m_state);
    if (auth.isEmpty() || !QDesktopServices::openUrl(auth)) {
        m_listener.close();
        m_clientId = m_previousClientId;
        m_previousClientId.clear();
        m_verifier.fill('\0');
        m_verifier.clear();
        m_state.fill('\0');
        m_state.clear();
        emit status("Could not open the official Google consent page in the system browser.");
        return false;
    }
    m_authTimeout.start(180000);
    emit status("Google consent opened in system browser. Return to Ember "
                "after choosing an account. No passwords enter Ember.");
    return true;
}
void Client::acceptOAuthConnection() {
    while (m_listener.hasPendingConnections()) {
        QTcpSocket *socket = m_listener.nextPendingConnection();
        if (!socket) continue;
        auto body = std::make_shared<QByteArray>();
        auto handled = std::make_shared<bool>(false);
        connect(socket, &QTcpSocket::readyRead, this,
            [this, socket, body, handled]() {
            if (*handled) return;
            body->append(socket->readAll());
            if (body->size() > 4096) {
                *handled = true;
                socket->disconnectFromHost();
                return;
            }
            const qsizetype delimiter = body->indexOf("\r\n\r\n");
            if (delimiter < 0) return;
            *handled = true;
            QString code;
            const bool authorized = validOAuthCallback(
                body->left(delimiter + 4), m_listener.serverPort(), m_state, &code);
            const QByteArray message = authorized
                ? QByteArray("Google Drive authorization received. Return to Ember.")
                : QByteArray("Invalid local authorization callback.");
            const QByteArray response =
                QByteArray(authorized ? "HTTP/1.1 200 OK\r\n" :
                                       "HTTP/1.1 400 Bad Request\r\n") +
                "Content-Type: text/plain; charset=utf-8\r\n"
                "Cache-Control: no-store\r\n"
                "Connection: close\r\n"
                "Content-Length: " + QByteArray::number(message.size()) +
                "\r\n\r\n" + message;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
            if (authorized) {
                m_listener.close();
                m_authTimeout.stop();
                m_state.fill('\0');
                m_state.clear();
                exchangeAuthorizationCode(code);
            }
        });
        connect(socket, &QTcpSocket::disconnected,
                socket, &QTcpSocket::deleteLater);
    }
}
void Client::exchangeAuthorizationCode(const QString &code) {
    const QByteArray params = postForm(m_clientId, "authorization_code",
                                       code, m_verifier, m_redirect);
    m_verifier.fill('\0');
    m_verifier.clear();
    postToken(params, false);
}
void Client::postToken(const QByteArray &form, bool refresh) {
    if (m_request) return;
    QNetworkRequest req(QUrl("https://oauth2.googleapis.com/token"));
    req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");
    QNetworkReply *reply = m_network.post(req, form);
    m_request = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
        [reply](qint64 received, qint64) {
            if (received > 16*1024) reply->abort();
        });
    connect(reply, &QNetworkReply::finished, this, [this, reply, refresh]() {
        if (m_request != reply) { reply->deleteLater(); return; }
        m_request = nullptr;
        const int http = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        const QByteArray bytes = reply->bytesAvailable() <= 16*1024
            ? reply->readAll() : QByteArray();
        reply->deleteLater();
        const auto json = QJsonDocument::fromJson(bytes);
        const auto obj = json.isObject() ? json.object() : QJsonObject();
        const QByteArray access = obj.value("access_token").toString().toLatin1();
        const QByteArray newRefresh =
            obj.value("refresh_token").toString().toLatin1();
        const QString granted = obj.value("scope").toString();
        if (error != QNetworkReply::NoError || http != 200 ||
            !validSecret(access) ||
            (!granted.isEmpty() &&
             !granted.split(' ').contains(QString::fromLatin1(kScope)))) {
            emit status("Google OAuth token exchange failed. Verify Desktop "
                        "OAuth client, consent and network access.");
            m_folderAfterRefresh.clear();
            if (!refresh) {
                m_clientId = m_previousClientId;
                m_previousClientId.clear();
            }
            return;
        }
        m_accessToken.fill('\0');
        m_accessToken = access;
        m_accessExpires = QDateTime::currentSecsSinceEpoch() +
            qMax(30, obj.value("expires_in").toInt(3600) - 60);
        bool stored = true;
        if (refresh) {
            if (!newRefresh.isEmpty()) stored = saveRefreshToken(newRefresh);
        } else {
            // A new account must never reuse an old account's refresh token.
            // If consent omitted offline access, clear the old credential.
            stored = newRefresh.isEmpty()
                ? deleteRefreshToken() : saveRefreshToken(newRefresh);
            if (!stored) deleteRefreshToken();
            m_previousClientId.clear();
        }
        if (!stored)
            emit status("Connected for this session, but Windows could not "
                        "save the Google refresh credential.");
        if (refresh) {
            const QString folder = m_folderAfterRefresh;
            m_folderAfterRefresh.clear();
            if (!folder.isEmpty()) requestFolder(folder);
        } else {
            verifyAccount();
        }
    });
}
void Client::requestRefresh() {
    if (m_request) return;
    QByteArray token;
    if (!readRefreshToken(token)) {
        m_folderAfterRefresh.clear();
        emit status("Google Drive session expired. Connect the account again.");
        return;
    }
    const QByteArray form = postForm(
        m_clientId, "refresh_token", QString::fromLatin1(token), {}, {});
    token.fill('\0');
    postToken(form, true);
}
void Client::getDrive(const QUrl &url, bool about, const QString &folderId) {
    if (m_request || m_accessToken.isEmpty()) return;
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_accessToken);
    request.setRawHeader("User-Agent", "EmberLocalDrive/0.1");
    QNetworkReply *reply = m_network.get(request);
    m_request = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
        [reply](qint64 received, qint64) {
            if (received > kMaximumResultBytes) reply->abort();
        });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, about, folderId]() {
        if (m_request != reply) { reply->deleteLater(); return; }
        m_request = nullptr;
        const int statusCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError = reply->error();
        const QByteArray body = reply->bytesAvailable() <= kMaximumResultBytes
            ? reply->readAll() : QByteArray();
        reply->deleteLater();
        if (networkError != QNetworkReply::NoError || statusCode != 200) {
            emit status(QString("Google Drive read failed (HTTP %1). Check "
                                "account permissions and regional access.")
                            .arg(statusCode));
            return;
        }
        if (about) {
            const QJsonDocument json = QJsonDocument::fromJson(body);
            const QString account = json.object().value("user")
                .toObject().value("emailAddress").toString();
            emit status(account.isEmpty()
                ? "Google Drive connected. Account email could not be verified."
                : "Google Drive connected as " + clean(account, 120) +
                  ". Metadata-only, user-initiated reads.");
            return;
        }
        const auto preview = parseFolder(folderId, body);
        if (!preview.ok()) {
            emit status(preview.error);
            return;
        }
        emit previewReady(preview.text);
        emit status(QString("Google Drive folder returned %1 metadata entries. "
                            "Review before inserting into your local chat.")
                        .arg(preview.count));
    });
}
void Client::verifyAccount() {
    getDrive(QUrl("https://www.googleapis.com/drive/v3/about?"
                  "fields=user(emailAddress,displayName)"), true);
}
void Client::requestFolder(const QString &folderId) {
    const QUrl url = folderListUrl(folderId);
    if (url.isEmpty()) {
        emit status("Invalid Google Drive folder ID.");
        return;
    }
    getDrive(url, false, folderId);
}
bool Client::listFolder(const QString &folderId, const QString &clientId) {
    if (!validDesktopClientId(clientId) || folderListUrl(folderId).isEmpty()) {
        emit status("Select a valid Desktop OAuth client and Drive folder ID.");
        return false;
    }
    if (m_request || m_listener.isListening()) {
        emit status("Finish the current Google Drive operation first.");
        return false;
    }
    m_clientId = clientId;
    if (m_accessToken.isEmpty() ||
        QDateTime::currentSecsSinceEpoch() >= m_accessExpires) {
        m_folderAfterRefresh = folderId;
        requestRefresh(); // only after the user's explicit browse click
    } else {
        requestFolder(folderId);
    }
    return true;
}
void Client::disconnectLocal() {
    if (m_request) {
        QNetworkReply *reply = m_request;
        m_request = nullptr;
        reply->abort();
    }
    m_listener.close();
    m_authTimeout.stop();
    m_clientId.clear();
    m_previousClientId.clear();
    m_redirect.clear();
    m_verifier.fill('\0');
    m_verifier.clear();
    m_state.fill('\0');
    m_state.clear();
    m_accessToken.fill('\0');
    m_accessToken.clear();
    m_accessExpires = 0;
    m_folderAfterRefresh.clear();
    const bool cleared = deleteRefreshToken();
    emit status(cleared
        ? "Google Drive credential removed locally. Revoke access in your "
          "Google Account separately if desired."
        : "Could not clear the stored Google Drive credential.");
}
}
