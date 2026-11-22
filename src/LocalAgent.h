#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

namespace EmberAgent {
enum class Scope { LocalProject, GitHubRepository };
// A deliberately tiny on-demand READ-ONLY tool protocol. It is not a
// general-purpose filesystem, browser, GitHub or command execution agent.
struct Decision {
    enum class Kind { Final, Call, Error };
    Kind kind = Kind::Error;
    QString text;
    QString tool;
    QString callId;
    QString query;
    QString path;
    QString view;
    int startLine = 1;
    qint64 number = 0;
    QString error;
    bool ok() const { return kind != Kind::Error; }
};
QJsonObject modelRequest(const QJsonArray &messages, int callsRemaining,
                         Scope scope = Scope::LocalProject);
Decision parseModelResponse(const QByteArray &json,
                            Scope scope = Scope::LocalProject);
QJsonObject assistantToolMessage(const Decision &decision);
QJsonObject toolResultMessage(const Decision &decision, const QString &result);
// Does NOT read outside the user-selected root. All file traversal uses
// the existing symlink/credential/budget-limited local search module.
QString executeReadOnly(const QString &selectedRoot, const Decision &decision);
// Converts a model-requested *read-only* view into the single GitHub REST URL
// for the user-selected repository; refuses all unrecognized modes/paths.
bool resolveGitHubRead(const QString &selectedRepository,
                       const Decision &decision, QUrl &endpoint,
                       int &viewNumber, QString &filePath);
}
