#include "Core.h"
#include "SessionStore.h"
#include <QTemporaryDir>
#include <QJsonArray>
#include <QTest>
#include <cstring>
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void localSqliteHistoryPersistsAndDeletesOnlySelectedSession() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filename = dir.filePath("chats.sqlite");
        QString alice;
        QString bob;
        {
            SessionStore store(filename);
            QVERIFY2(store.ready(), qPrintable(store.error()));
            alice = store.createSession();
            bob = store.createSession();
            QVERIFY(!alice.isEmpty());
            QVERIFY(!bob.isEmpty());
            QVERIFY(alice != bob);
            QVERIFY(store.append(alice, "you", "My first storytelling idea."));
            QVERIFY(store.append(alice, "ember", "Tell me more about your character."));
            QVERIFY(store.append(alice, "you", "She explores a city."));
            QVERIFY(store.append(bob, "you", "Separate troubleshooting chat"));
            QCOMPARE(store.totalMessages(alice), qint64(3));
            QCOMPARE(store.totalMessages(bob), qint64(1));
            QVERIFY(!store.append(bob, "system", "forbidden role"));
            QVERIFY(!store.append("wrong-id", "you", ""));
            const auto first = store.messages(alice);
            QCOMPARE(first.size(), 3);
            QCOMPARE(first.first().toMap().value("role").toString(), "you");
            QCOMPARE(first.last().toMap().value("body").toString(), "She explores a city.");
            QVERIFY(store.sessions().size() >= 2);
            QVERIFY(store.sessions().size() <= 200);
        }
        {
            SessionStore restored(filename);
            QVERIFY2(restored.ready(), qPrintable(restored.error()));
            QVERIFY(restored.exists(alice));
            QCOMPARE(restored.totalMessages(alice), qint64(3));
            const auto summaries = restored.sessions();
            bool named = false;
            for (const auto &item : summaries) {
                auto row = item.toMap();
                if (row.value("id").toString() == alice) {
                    named = row.value("title").toString() == "My first storytelling idea.";
                }
            }
            QVERIFY(named);
            QVERIFY(restored.remove(alice));
            QVERIFY(!restored.exists(alice));
            QCOMPARE(restored.totalMessages(alice), qint64(0));
            QVERIFY(restored.exists(bob));
            QCOMPARE(restored.totalMessages(bob), qint64(1));
        }
    }
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
    void longSpokenRepliesAreSplitWithoutSilentTruncation() {
        const QString input = QString("A thoughtful reply has a natural pause. ").repeated(70).trimmed();
        const auto chunks = EmberCore::speechChunks(input);
        QVERIFY(chunks.size() > 2);
        for (const auto &chunk : chunks) {
            QVERIFY(!chunk.isEmpty());
            QVERIFY(chunk.size() <= 700);
            QVERIFY(chunk.endsWith('.') || chunk == chunks.last());
        }
        QCOMPARE(chunks.join(" "), input);
        const auto forced = EmberCore::speechChunks(QString(1500, 'x'), 600);
        QVERIFY(forced.size() >= 3);
        for (const auto &chunk : forced) QVERIFY(chunk.size() <= 600);
        QVERIFY(EmberCore::speechChunks({}).isEmpty());
        QVERIFY(EmberCore::speechChunks(QString(17000, 'y')).isEmpty());
    }
    void localSpeechBoundaryAndRequestShape() {
        QVERIFY(EmberCore::allowedLocalSpeechEndpoint(
            "http://127.0.0.1:43822/v1/audio/speech"));
        for (const auto &url : {
             "https://127.0.0.1:43822/v1/audio/speech",
             "http://localhost:43822/v1/audio/speech",
             "http://127.0.0.1:43823/v1/audio/speech",
             "http://127.0.0.1:43822/admin",
             "http://127.0.0.1:43822/v1/audio/speech?forward=1",
             "http://user:secret@127.0.0.1:43822/v1/audio/speech"
             }) QVERIFY(!EmberCore::allowedLocalSpeechEndpoint(url));
        const auto payload = EmberCore::speechRequest("qwen3-tts","Vivian","Hello, Ember");
        QCOMPARE(payload.value("voice").toString(), QString("Vivian"));
        QCOMPARE(payload.value("model").toString(), QString("qwen3-tts"));
        QCOMPARE(payload.value("response_format").toString(), QString("wav"));
        QCOMPARE(payload.value("input").toString(), QString("Hello, Ember"));
        QVERIFY(!payload.contains("url"));
    }
    void rejectsInvalidSpeechAudio() {
        const auto good = EmberCore::pcmToWav(QByteArray::fromHex("0100020003000400"), 24000);
        QVERIFY(EmberCore::validSpeechWav(good));
        QVERIFY(!EmberCore::validSpeechWav({}));
        QVERIFY(!EmberCore::validSpeechWav(QByteArray("JSON response from server")));
        QByteArray corrupted = good;
        corrupted[4] = char(1);
        QVERIFY(!EmberCore::validSpeechWav(corrupted));
        corrupted = good;
        corrupted[20] = char(3); // not standard PCM16
        QVERIFY(!EmberCore::validSpeechWav(corrupted));
        corrupted = good.left(good.size() - 1);
        QVERIFY(!EmberCore::validSpeechWav(corrupted));
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
        prior.append(EmberCore::Turn{"Question", "Previous answer"});
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
