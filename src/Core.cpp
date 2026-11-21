#include "Core.h"
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
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
    // Retain as many recent complete exchanges as fit the bounded request.
    // This avoids both the old fixed eight-turn memory loss and accidental
    // requests larger than the 8192-token llama.cpp context. Character count
    // is a conservative heuristic, not a tokenizer / exact context guarantee.
    const QString currentUser = user.left(4096);
    int remainingChars = std::max(0, 12000 - persona.left(4096).size()
                                  - currentUser.size());
    QList<Turn> selected;
    for (int i = history.size() - 1; i >= 0 && selected.size() < 24; --i) {
        const auto &turn = history.at(i);
        const QString oldUser = turn.user.left(4096);
        QString oldAssistant = turn.assistant.right(8192);
        const int cost = oldUser.size() + oldAssistant.size() + 96;
        if (cost > remainingChars) {
            // Preserve the last assistant continuation when it is too large,
            // instead of silently dropping the entire immediately prior turn.
            if (selected.isEmpty() && remainingChars > oldUser.size() + 384) {
                oldAssistant = oldAssistant.right(remainingChars - oldUser.size() - 96);
                selected.prepend(Turn{oldUser, oldAssistant});
            }
            break;
        }
        selected.prepend(Turn{oldUser, oldAssistant});
        remainingChars -= cost;
    }
    for (const auto &turn : selected) {
        messages.append(QJsonObject{{"role", "user"}, {"content", turn.user}});
        messages.append(QJsonObject{{"role", "assistant"}, {"content", turn.assistant}});
    }
    messages.append(QJsonObject{{"role", "user"}, {"content", currentUser}});
    return QJsonObject{{"model", "ember"}, {"messages", messages},
                       {"max_tokens", std::clamp(maxTokens, 64, 2048)},
                       {"temperature", std::isfinite(temperature) ? std::clamp(temperature, 0.1, 1.5) : 0.8},
                       {"stream", false}};
}
// A shared offline speech adapter deliberately has no configurable remote
// host and rejects redirects in the network client. Qwen3, Chatterbox and
// compatible local TTS servers may implement this exact small contract.
bool allowedLocalSpeechEndpoint(const QString &raw) {
    const QUrl url(raw);
    return url.isValid() && url.scheme() == "http" &&
        url.host() == "127.0.0.1" && url.port() == 43822 &&
        url.userName().isEmpty() && url.password().isEmpty() &&
        url.query().isEmpty() && url.fragment().isEmpty() &&
        url.path() == "/v1/audio/speech";
}
QJsonObject speechRequest(const QString &model, const QString &voice,
                          const QString &text) {
    return QJsonObject{
        {"model", model.left(80)},
        {"voice", voice.left(80)},
        {"input", text.left(700)},
        {"response_format", "wav"}
    };
}
// Validate a bounded, ordinary PCM16 RIFF/WAV before writing to private
// scratch storage and handing the file to Windows Multimedia. Do not treat
// JSON/HTML error responses as audio. Accept optional WAV metadata chunks.
bool validSpeechWav(const QByteArray &wav) {
    if (wav.size() < 46 || wav.size() > 8 * 1024 * 1024 ||
        wav.left(4) != "RIFF" || wav.mid(8, 4) != "WAVE") return false;
    auto u16 = [&](qsizetype at) {
        return quint16(quint8(wav[at])) |
            (quint16(quint8(wav[at + 1])) << 8);
    };
    auto u32 = [&](qsizetype at) {
        return quint32(quint8(wav[at])) |
            (quint32(quint8(wav[at + 1])) << 8) |
            (quint32(quint8(wav[at + 2])) << 16) |
            (quint32(quint8(wav[at + 3])) << 24);
    };
    if (u32(4) != quint32(wav.size() - 8)) return false;
    bool format = false, data = false;
    quint16 blockAlign = 0;
    for (qsizetype pos = 12; pos + 8 <= wav.size();) {
        const quint32 len = u32(pos + 4);
        const qsizetype start = pos + 8;
        if (len > quint32(wav.size() - start)) return false;
        const auto tag = wav.mid(pos, 4);
        if (tag == "fmt ") {
            if (len < 16) return false;
            const quint16 pcm = u16(start);
            const quint16 channels = u16(start + 2);
            const quint32 rate = u32(start + 4);
            blockAlign = u16(start + 12);
            const quint16 bits = u16(start + 14);
            if (pcm != 1 || channels < 1 || channels > 2 ||
                rate < 8000 || rate > 96000 || bits != 16 ||
                blockAlign != channels * 2) return false;
            format = true;
        } else if (tag == "data") {
            if (len == 0) return false;
            data = true;
        }
        const qsizetype next = start + len + (len % 2);
        if (next <= pos || next > wav.size()) return false;
        pos = next;
    }
    return format && data && blockAlign > 0;
}
// Preserve the content of a long spoken reply instead of silently clipping
// everything after the first 700 characters. Prefer natural sentence ends,
// then word boundaries. Never pass more than 700 UTF-16 code units to either
// offline TTS provider. The API stays turn-based, not fake full duplex.
QStringList speechChunks(const QString &input, int maxCharacters) {
    const QString text = input.simplified();
    if (text.isEmpty() || text.size() > 16384) return {};
    const int limit = std::clamp(maxCharacters, 80, 700);
    QStringList chunks;
    for (qsizetype pos = 0; pos < text.size();) {
        qsizetype end = std::min<qsizetype>(pos + limit, text.size());
        if (end < text.size()) {
            // Never divide a Unicode surrogate pair in a long unbroken word.
            if (text[end - 1].isHighSurrogate() && text[end].isLowSurrogate()) --end;
            qsizetype sentence = -1;
            for (qsizetype at = end - 1; at >= pos + limit / 3; --at) {
                const QChar ch = text[at];
                if ((ch == '.' || ch == '!' || ch == '?' || ch == ';') &&
                    (at + 1 == text.size() || text[at + 1].isSpace())) {
                    sentence = at + 1;
                    break;
                }
            }
            if (sentence > pos) end = sentence;
            else {
                const qsizetype space = text.lastIndexOf(' ', end);
                if (space > pos + limit / 3) end = space;
            }
        }
        const QString chunk = text.mid(pos, end - pos).trimmed();
        if (!chunk.isEmpty()) chunks.append(chunk);
        pos = end;
        while (pos < text.size() && text[pos].isSpace()) ++pos;
    }
    return chunks;
}
StreamResult consumeChatStream(QByteArray &buffer, const QByteArray &incoming) {
    StreamResult result;
    if (incoming.size() > 128 * 1024 || buffer.size() + incoming.size() > 256 * 1024) {
        result.error = true;
        buffer.clear();
        return result;
    }
    buffer.append(incoming);
    for (;;) {
        const auto lf = buffer.indexOf("\n\n");
        const auto crlf = buffer.indexOf("\r\n\r\n");
        const bool useCrlf = crlf >= 0 && (lf < 0 || crlf < lf);
        const qsizetype end = useCrlf ? crlf : lf;
        if (end < 0) break;
        const QByteArray frame = buffer.left(end);
        buffer.remove(0, end + (useCrlf ? 4 : 2));
        const QList<QByteArray> lines = frame.split('\n');
        for (QByteArray line : lines) {
            line = line.trimmed();
            if (!line.startsWith("data:")) continue;
            const QByteArray data = line.mid(5).trimmed();
            if (data == "[DONE]") {
                result.done = true;
                buffer.clear();
                return result;
            }
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                result.error = true;
                buffer.clear();
                return result;
            }
            const QJsonObject envelope = doc.object();
            if (envelope.contains("error")) {
                result.error = true;
                buffer.clear();
                return result;
            }
            const QJsonArray choices = envelope.value("choices").toArray();
            if (choices.isEmpty()) continue;
            const QJsonObject delta = choices.first().toObject().value("delta").toObject();
            const QString text = delta.value("content").toString();
            if (!text.isEmpty()) result.text += text;
        }
    }
    return result;
}
bool allowedLocalEndpoint(const QString &raw) {
    const QUrl url(raw);
    return url.isValid() && url.scheme() == "http" && url.host() == "127.0.0.1" &&
           url.port() == 43821 && url.userName().isEmpty() && url.password().isEmpty() &&
           url.query().isEmpty() && url.fragment().isEmpty() &&
           (url.path() == "/health" || url.path() == "/v1/chat/completions");
}
}
