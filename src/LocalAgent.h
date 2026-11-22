#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace EmberAgent {
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
    int startLine = 1;
    QString error;
    bool ok() const { return kind != Kind::Error; }
};
QJsonObject modelRequest(const QJsonArray &messages, int callsRemaining);
Decision parseModelResponse(const QByteArray &json);
QJsonObject assistantToolMessage(const Decision &decision);
QJsonObject toolResultMessage(const Decision &decision, const QString &result);
// Does NOT read outside the user-selected root. All file traversal uses
// the existing symlink/credential/budget-limited local search module.
QString executeReadOnly(const QString &selectedRoot, const Decision &decision);
}
