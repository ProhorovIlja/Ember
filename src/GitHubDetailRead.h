#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>

namespace EmberGitHubDetails {
enum class Kind { Issue, Pull, RunJobs };
struct Preview {
    QString text;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
// GitHub API endpoints are constructed from one separately user-approved
// repository and a positive numeric identifier; no model-authored URLs.
QUrl detailUrl(const QString &selectedRepository, Kind kind, qint64 number);
Preview parseDetail(const QString &selectedRepository, Kind kind,
                    qint64 number, const QByteArray &payload);
}
