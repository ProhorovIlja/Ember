#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QSet>

namespace EmberAgent {
enum class Scope { LocalProject, GitHubRepository, WikipediaResearch };
struct SourcePatch { QString path, oldText, newText; };
struct NewSourceFile { QString path, content; };
// A deliberately tiny on-demand READ-ONLY tool protocol. It is not a
// general-purpose filesystem, browser, GitHub or command execution agent.
struct Decision {
    enum class Kind { Final, Call, Error };
    Kind kind = Kind::Error;
    QString text;
    QString tool;
    QString callId;
    QString query;
    QString oldText;
    QString newText;
    QVector<SourcePatch> patches;
    QVector<NewSourceFile> newFiles;
    QString path;
    QString view;
    int startLine = 1;
    qint64 number = 0;
    QString error;
    bool ok() const { return kind != Kind::Error; }
};
QJsonObject modelRequest(const QJsonArray &messages, int callsRemaining,
                         Scope scope = Scope::LocalProject,
                         bool mayProposeEdit = false,
                         bool enableThinking = false);
Decision parseModelResponse(const QByteArray &json,
                            Scope scope = Scope::LocalProject,
                            bool mayProposeEdit = false);
// Check against successful read_source calls in the current foreground task.
// This is not permission to access any additional file: the project-root
// and exact-snapshot validators still run before staging or writing.
bool patchSourcesWereInspected(const QSet<QString> &readPaths,
                               const Decision &decision);
QString normalizedInspectedSourcePath(const QString &path);
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
