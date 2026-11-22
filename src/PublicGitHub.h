#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>

namespace EmberGitHub {
enum class View { Overview = 0, Issues = 1, PullRequests = 2, File = 3,
                  Branches = 4, Forks = 5, WorkflowRuns = 6 };
struct Preview {
    QString text;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
// Only GitHub's official HTTPS public REST host; never arbitrary URLs,
// authenticated requests, user-provided query strings or redirects.
QUrl publicRequestUrl(const QString &repository, View view,
                      const QString &filePath = {});
Preview parsePublicResponse(const QString &repository, View view,
                            const QString &filePath, const QByteArray &data);
}
