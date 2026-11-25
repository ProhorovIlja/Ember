#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>

namespace EmberGitHubWrite {
struct IssueDraft {
    QUrl endpoint;
    QByteArray json;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
struct CreatedIssue {
    QString url;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
// No model-driven writes. Caller must show the complete body and obtain a
// separate, default-deny user confirmation before sending the returned JSON.
IssueDraft prepareIssue(const QString &repository, const QString &title,
                        const QString &body);
CreatedIssue parseCreatedIssue(const QString &repository,
                               int httpStatus, const QByteArray &response);
}
