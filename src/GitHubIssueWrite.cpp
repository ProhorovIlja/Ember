#include "GitHubIssueWrite.h"
#include "PublicGitHub.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace EmberGitHubWrite {
IssueDraft prepareIssue(const QString &repository, const QString &title,
                        const QString &body) {
    IssueDraft draft;
    QUrl endpoint = EmberGitHub::publicRequestUrl(
        repository, EmberGitHub::View::Issues);
    if (endpoint.isEmpty() || title.trimmed().isEmpty() ||
        title.size() > 240 || body.trimmed().isEmpty() ||
        body.size() > 12000 || title.contains(QChar::Null) ||
        body.contains(QChar::Null)) {
        draft.error = "Invalid GitHub repo, title or issue body "
                      "(maximum 240/12,000 characters).";
        return draft;
    }
    // The GET URL contains pagination/query parameters, but POST must not.
    endpoint.setQuery(QString());
    if (endpoint.scheme() != "https" ||
        endpoint.host() != "api.github.com" ||
        !endpoint.userInfo().isEmpty() ||
        endpoint.port(-1) != -1 ||
        !endpoint.query().isEmpty() || !endpoint.fragment().isEmpty() ||
        endpoint.path() != "/repos/" + repository + "/issues") {
        draft.error = "Refusing a GitHub write outside the strict REST issue endpoint.";
        return draft;
    }
    draft.endpoint = endpoint;
    draft.json = QJsonDocument(QJsonObject{
        {"title", title.trimmed()}, {"body", body}
    }).toJson(QJsonDocument::Compact);
    if (draft.json.size() > 65536) {
        draft.endpoint = {};
        draft.json.clear();
        draft.error = "GitHub JSON body exceeds the 64 KiB safety limit.";
    }
    return draft;
}
CreatedIssue parseCreatedIssue(const QString &repository,
                               int httpStatus, const QByteArray &response) {
    if (httpStatus != 201 || response.isEmpty() ||
        response.size() > 65536 ||
        EmberGitHub::publicRequestUrl(
            repository, EmberGitHub::View::Issues).isEmpty())
        return {{}, "GitHub did not verify issue creation (HTTP " +
                    QString::number(httpStatus) + ")."};
    QJsonParseError error{};
    const QJsonDocument json = QJsonDocument::fromJson(response, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        return {{}, "GitHub returned malformed JSON for the issue."};
    const QJsonObject object = json.object();
    const int number = object.value("number").toInt();
    const QUrl url(object.value("html_url").toString());
    if (number <= 0 || !url.isValid() || url.scheme() != "https" ||
        url.host() != "github.com" || !url.userInfo().isEmpty() ||
        url.port(-1) != -1 || !url.query().isEmpty() ||
        !url.fragment().isEmpty() ||
        url.path().compare("/" + repository + "/issues/" +
                           QString::number(number), Qt::CaseInsensitive) != 0)
        return {{}, "GitHub issue URL did not match the selected repository."};
    const QUrl owner(object.value("repository_url").toString());
    if (!owner.isValid() || owner.scheme() != "https" ||
        owner.host() != "api.github.com" || !owner.userInfo().isEmpty() ||
        owner.port(-1) != -1 ||
        !owner.query().isEmpty() || !owner.fragment().isEmpty() ||
        owner.path().compare("/repos/" + repository, Qt::CaseInsensitive) != 0)
        return {{}, "GitHub repository identity did not match the issue."};
    return {url.toString(QUrl::FullyEncoded), {}};
}
}
