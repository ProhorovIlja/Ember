#include "GitHubCredential.h"
#include <QRegularExpression>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace EmberCredential {
namespace {
bool allowedTarget(const QString &target) {
    if (target == QString::fromLatin1(kGitHubCredentialTarget)) return true;
    static const QRegularExpression testTarget(
        QStringLiteral("^Ember\\.Local\\.GitHub\\.Test\\.[A-Za-z0-9-]{1,50}$"));
    return testTarget.match(target).hasMatch();
}
}
bool validToken(const QByteArray &token) {
    if (token.size() < 20 || token.size() > 256) return false;
    for (const char c : token) {
        const auto ch = static_cast<unsigned char>(c);
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
    }
    return true;
}
bool saveGitHubToken(const QByteArray &token, const QString &target) {
    if (!allowedTarget(target) || !validToken(token)) return false;
#ifdef Q_OS_WIN
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(
        reinterpret_cast<LPCWSTR>(target.utf16()));
    credential.CredentialBlobSize = DWORD(token.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(
        const_cast<char *>(token.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"GitHub read-only account");
    return CredWriteW(&credential, 0) != 0;
#else
    return false; // No plaintext fallback; target product is Windows.
#endif
}
bool readGitHubToken(QByteArray &token, const QString &target) {
    token.clear();
    if (!allowedTarget(target)) return false;
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()),
                   CRED_TYPE_GENERIC, 0, &credential)) return false;
    bool ok = false;
    if (credential && credential->CredentialBlobSize <= 256 &&
        credential->CredentialBlob) {
        QByteArray candidate(
            reinterpret_cast<const char *>(credential->CredentialBlob),
            qsizetype(credential->CredentialBlobSize));
        if (validToken(candidate)) {
            token = candidate;
            ok = true;
        }
        candidate.fill('\0');
    }
    if (credential) CredFree(credential);
    return ok;
#else
    return false;
#endif
}
bool deleteGitHubToken(const QString &target) {
    if (!allowedTarget(target)) return false;
#ifdef Q_OS_WIN
    if (CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()),
                    CRED_TYPE_GENERIC, 0)) return true;
    return GetLastError() == ERROR_NOT_FOUND;
#else
    return false;
#endif
}
}
