#include "Core.h"
#include <QJsonArray>
#include <QTest>
#include <cstring>
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void wavHeaderAndAudio() {
        const QByteArray pcm = QByteArray::fromHex("0000ff7f00800040");
        const auto wav = EmberCore::pcmToWav(pcm, 16000);
        QCOMPARE(wav.size(), 52);
        QCOMPARE(wav.left(4), QByteArray("RIFF"));
        QCOMPARE(wav.mid(8, 4), QByteArray("WAVE"));
        QCOMPARE(wav.mid(36, 4), QByteArray("data"));
        QCOMPARE(wav.mid(44), pcm);
        QCOMPARE(EmberCore::pcmToWav({}, 16000), QByteArray());
        QCOMPARE(EmberCore::pcmToWav(QByteArray("odd"), 16000), QByteArray());
        QCOMPARE(EmberCore::pcmToWav(pcm, 0), QByteArray());
    }
    void resamplesCommonWindowsStereoMicrophone() {
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);
        QByteArray audio;
        audio.resize(480 * 4);
        for (qsizetype i = 0; i < audio.size(); i += 4) {
            audio[i] = 0;
            audio[i+1] = 0x40;
            audio[i+2] = 0;
            audio[i+3] = 0x40;
        }
        const auto wav = EmberCore::whisperWav(audio, format);
        QCOMPARE(wav.size(), 44 + 160 * 2);
        QCOMPARE(wav.left(4), QByteArray("RIFF"));
        const int byte = 44 + 80 * 2;
        const qint16 center = qint16(quint16(quint8(wav[byte])) |
                              quint16(quint8(wav[byte + 1])) << 8);
        QVERIFY(center > 15000 && center < 17000);
        QVERIFY(EmberCore::whisperWav(audio.left(audio.size()-1), format).isEmpty());
        format.setSampleRate(0);
        QVERIFY(EmberCore::whisperWav(audio, format).isEmpty());
        format.setSampleRate(48000);
        format.setSampleFormat(QAudioFormat::Float);
        QByteArray floats;
        floats.resize(480 * 2 * 4);
        for (qsizetype i = 0; i < floats.size(); i += 4) {
            const float sample = 0.25f;
            std::memcpy(floats.data() + i, &sample, 4);
        }
        QCOMPARE(EmberCore::whisperWav(floats, format).size(), 44 + 160 * 2);
    }
    void localOnlyEndpoint() {
        QVERIFY(EmberCore::allowedLocalEndpoint("http://127.0.0.1:43821/health"));
        QVERIFY(EmberCore::allowedLocalEndpoint("http://127.0.0.1:43821/v1/chat/completions"));
        QVERIFY(!EmberCore::allowedLocalEndpoint("http://0.0.0.0:43821/health"));
        QVERIFY(!EmberCore::allowedLocalEndpoint("http://evil.example:43821/v1/chat/completions"));
        QVERIFY(!EmberCore::allowedLocalEndpoint("https://127.0.0.1:43821/health"));
        QVERIFY(!EmberCore::allowedLocalEndpoint("http://127.0.0.1:43821/health?secret=x"));
    }
    void modelMessageRolesAreIsolated() {
        QList<EmberCore::Turn> prior;
        prior.append({"Question", "Previous answer"});
        const auto payload = EmberCore::chatRequest("Talk naturally", prior, "Do not merge me into system", 900, 0.7);
        QCOMPARE(payload.value("model").toString(), QString("ember"));
        QCOMPARE(payload.value("max_tokens").toInt(), 768);
        const auto messages = payload.value("messages").toArray();
        QCOMPARE(messages.size(), 4);
        QCOMPARE(messages.at(0).toObject().value("role").toString(), QString("system"));
        QCOMPARE(messages.at(1).toObject().value("role").toString(), QString("user"));
        QCOMPARE(messages.at(2).toObject().value("role").toString(), QString("assistant"));
        QCOMPARE(messages.at(3).toObject().value("content").toString(), QString("Do not merge me into system"));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "CoreTests.moc"
