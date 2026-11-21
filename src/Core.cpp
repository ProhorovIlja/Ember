#include "Core.h"
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
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
// Band-limited conversion of interleaved native capture into the PCM format
// whisper.cpp expects. Avoid a silent wrong-sample-rate WAV header.
QByteArray whisperWav(const QByteArray &raw, const QAudioFormat &source) {
    if (!source.isValid() || source.sampleRate() < 8000 ||
        source.sampleRate() > 96000 || source.channelCount() < 1 ||
        source.channelCount() > 8 || source.sampleFormat() == QAudioFormat::Unknown ||
        raw.isEmpty() || raw.size() > 32 * 1024 * 1024 ||
        source.bytesPerFrame() <= 0 || raw.size() % source.bytesPerFrame()) return {};
    if (source.sampleRate() == 16000 && source.channelCount() == 1 &&
        source.sampleFormat() == QAudioFormat::Int16) return pcmToWav(raw, 16000);

    const qsizetype frames = raw.size() / source.bytesPerFrame();
    const qsizetype outFrames = (frames * 16000) / source.sampleRate();
    if (outFrames < 1 || outFrames > 16000 * 31) return {};
    const int channels = source.channelCount();
    const int bytes = source.bytesPerSample();
    if (bytes <= 0) return {};
    const char *sourceBytes = raw.constData();
    auto sampleAt = [&](qsizetype frame) -> double {
        double sum = 0.0;
        for (int channel = 0; channel < channels; ++channel) {
            const char *p = sourceBytes + frame * source.bytesPerFrame() + channel * bytes;
            double value = 0.0;
            switch (source.sampleFormat()) {
            case QAudioFormat::UInt8:
                value = (double(static_cast<quint8>(*p)) - 128.0) / 128.0;
                break;
            case QAudioFormat::Int16: {
                qint16 sample;
                std::memcpy(&sample, p, sizeof(sample));
                value = double(sample) / 32768.0;
                break;
            }
            case QAudioFormat::Int32: {
                qint32 sample;
                std::memcpy(&sample, p, sizeof(sample));
                value = double(sample) / 2147483648.0;
                break;
            }
            case QAudioFormat::Float: {
                float sample;
                std::memcpy(&sample, p, sizeof(sample));
                value = std::isfinite(sample)
                    ? std::clamp(double(sample), -1.0, 1.0) : 0.0;
                break;
            }
            default: return 0.0;
            }
            sum += value;
        }
        return sum / channels;
    };
    QByteArray pcm;
    pcm.resize(outFrames * 2);
    const double ratio = double(source.sampleRate()) / 16000.0;
    const double cutoff = std::min(1.0, 1.0 / ratio) * 0.93;
    constexpr int radius = 12;
    constexpr double pi = std::numbers::pi;
    for (qsizetype i = 0; i < outFrames; ++i) {
        const double location = (double(i) + 0.5) * ratio - 0.5;
        const qsizetype first = qsizetype(std::floor(location)) - radius + 1;
        double value = 0.0, weightSum = 0.0;
        for (qsizetype k = first; k < first + 2 * radius; ++k) {
            if (k < 0 || k >= frames) continue;
            const double distance = double(k) - location;
            const double phase = cutoff * distance;
            const double sinc = std::abs(phase) < 1e-9
                ? 1.0 : std::sin(pi * phase) / (pi * phase);
            const double window = 0.5 * (1.0 + std::cos(pi * distance / radius));
            const double weight = cutoff * sinc * window;
            value += sampleAt(k) * weight;
            weightSum += weight;
        }
        const double normalized = std::abs(weightSum) > 1e-9
            ? std::clamp(value / weightSum, -1.0, 1.0) : 0.0;
        const qint16 quantized = qint16(std::lround(normalized * 32767.0));
        pcm[i * 2] = char(quint16(quantized) & 0xff);
        pcm[i * 2 + 1] = char((quint16(quantized) >> 8) & 0xff);
    }
    return pcmToWav(pcm, 16000);
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
