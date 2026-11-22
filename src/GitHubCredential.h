#pragma once
#include <QByteArray>
#include <QString>

namespace EmberCredential {
// Fixed default target used only by the user's explicitly connected GitHub
// read-only workflow. A unique Ember.Local.GitHub.Test.* name is also allowed
// for isolated regression tests. No tokens are stored in QSettings or SQLite.
constexpr auto kGitHubCredentialTarget = "Ember.Local.GitHub.ReadOnly";
bool validToken(const QByteArray &token);
bool saveGitHubToken(const QByteArray &token,
                     const QString &target = QString::fromLatin1(kGitHubCredentialTarget));
bool readGitHubToken(QByteArray &token,
                     const QString &target = QString::fromLatin1(kGitHubCredentialTarget));
bool deleteGitHubToken(const QString &target =
                       QString::fromLatin1(kGitHubCredentialTarget));
}
