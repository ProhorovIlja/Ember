#include "Core.h"
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QUrl>
#include <algorithm>
#include <cmath>
namespace EmberCore {
QByteArray pcmToWav(const QByteArray &pcm, int sampleRate) {
    if (pcm.isEmpty() || (pcm.size() % 2) || pcm.size() > 32 * 1024 * 1024 ||
        sampleRate < 8000 || sampleRate > 96000) return {};
    QByteArray result;
    result.reserve(44 + pcm.size());
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + pcm.size());
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16) << quint16(1) << quint16(1);
    stream << quint32(sampleRate) << quint32(sampleRate * 2);
    stream << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << quint32(pcm.size());
    stream.writeRawData(pcm.constData(), pcm.size());
    return result;
}
QJsonObject chatRequest(const QString &persona, const QList<Turn> &history,
                        const QString &user, int maxTokens, double temperature) {
    QJsonArray messages;
    messages.append(QJsonObject{{"role", "system"}, {"content", persona.left(4096)}});
    const int beginning = std::max(0, int(history.size()) - 8);
    for (int i = beginning; i < history.size(); ++i) {
        const auto &turn = history.at(i);
        messages.append(QJsonObject{{"role", "user"}, {"content", turn.user.left(4096)}});
        messages.append(QJsonObject{{"role", "assistant"}, {"content", turn.assistant.left(16384)}});
    }
    messages.append(QJsonObject{{"role", "user"}, {"content", user.left(4096)}});
    return QJsonObject{{"model", "ember"}, {"messages", messages},
                       {"max_tokens", std::clamp(maxTokens, 64, 768)},
                       {"temperature", std::isfinite(temperature) ? std::clamp(temperature, 0.1, 1.5) : 0.8},
                       {"stream", false}};
}
bool allowedLocalEndpoint(const QString &raw) {
    const QUrl url(raw);
    return url.isValid() && url.scheme() == "http" && url.host() == "127.0.0.1" &&
           url.port() == 43821 && url.userName().isEmpty() && url.password().isEmpty() &&
           url.query().isEmpty() && url.fragment().isEmpty() &&
           (url.path() == "/health" || url.path() == "/v1/chat/completions");
}
}
