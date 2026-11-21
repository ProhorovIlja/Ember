#include "EmberBackend.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariantMap>

namespace {
QString appFolder() {
    return QCoreApplication::applicationDirPath();
}
QString defaultPath(const QString &key) {
    const QDir root(appFolder());
    if (key == "brainExe") return root.filePath("engines/llama/llama-server.exe");
    if (key == "brainModel") return root.filePath("models/llm/model.gguf");
    if (key == "asrExe") return root.filePath("engines/whisper/whisper-cli.exe");
    if (key == "asrModel") return root.filePath("models/stt/ggml-base.bin");
    if (key == "ttsExe") return root.filePath("engines/sherpa/sherpa-onnx-offline-tts.exe");
    if (key == "ttsDir") return root.filePath("models/tts/kokoro-en-v0_19");
    return {};
}
const QString kSystemPrompt =
    "You are Ember, a natural, thoughtful, locally running personal assistant. "
    "Use concise, fluid sentences in voice mode; be precise when the user needs detail. "
    "Answer in the language the user uses. You are not a human. "
    "Do not claim to have viewed local files or used programs unless you actually did.";
}

EmberBackend::EmberBackend(QObject *parent)
    : QObject(parent),
      m_settings(QDir(appFolder()).filePath("data/ember.ini"), QSettings::IniFormat) {
    QDir().mkpath(QDir(appFolder()).filePath("data"));
    QDir().mkpath(QDir(appFolder()).filePath("cache"));
    m_work = std::make_unique<QTemporaryDir>(
        QDir(appFolder()).filePath("cache/session-XXXXXX"));
    m_persona = m_settings.value("persona", kSystemPrompt).toString();
    m_voiceId = m_settings.value("voiceId", 0).toInt();
    m_voiceId = qBound(0, m_voiceId, 100);

    m_mediaPlayer.setAudioOutput(&m_audioOutput);
    m_audioOutput.setVolume(0.85);
    connect(&m_mediaPlayer, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (state == QMediaPlayer::StoppedState && m_speaking) {
                    setSpeaking(false);
                    setStatus("Ready for your next question.");
                }
            });
    connect(&m_mediaPlayer, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &) {
                if (error != QMediaPlayer::NoError) {
                    setSpeaking(false);
                    setStatus("Windows could not play the synthesized audio.");
                }
            });

    m_healthTimer.setInterval(1200);
    connect(&m_healthTimer, &QTimer::timeout, this, &EmberBackend::pollBrain);
    connect(&m_brain, &QProcess::finished, this,
            [this](int, QProcess::ExitStatus) {
                m_healthTimer.stop();
                if (m_brainReady) setBrainReady(false);
                if (m_busy || m_listening) cancelWork();
                setStatus("Local text engine stopped. Start the brain to continue.");
            });
    connect(&m_brain, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        setBrainReady(false);
        setStatus("Could not start the local llama-server executable.");
    });

    connect(&m_whisper, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus how) {
                if (!m_busy) return; // Cancelled tasks never trigger a message.
                if (how != QProcess::NormalExit || code != 0) {
                    setBusy(false);
                    setStatus("Speech recognition failed. Check whisper.cpp and the chosen model.");
                    return;
                }
                QFile file(workFile("recognition.txt"));
                if (!file.open(QIODevice::ReadOnly) || file.size() > 32768) {
                    setBusy(false);
                    setStatus("Speech recognition produced no readable transcript.");
                    return;
                }
                const QString transcript = QString::fromUtf8(file.readAll()).trimmed();
                if (transcript.isEmpty()) {
                    setBusy(false);
                    setStatus("No speech was recognized. Try recording again.");
                    return;
                }
                m_transcript = transcript;
                emit transcriptChanged();
                setBusy(false);
                submit(transcript, true);
            });
    connect(&m_whisper, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                if (m_busy) {
                    setBusy(false);
                    setStatus("whisper-cli did not start. Check its executable and DLL files.");
                }
            });

    connect(&m_tts, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus how) {
                if (!m_busy) return;
                setBusy(false);
                const QString audioFile = workFile("voice.wav");
                if (how != QProcess::NormalExit || code != 0 ||
                    QFileInfo(audioFile).size() < 44) {
                    setStatus("Voice synthesis failed. Check the Kokoro files and voice ID.");
                    return;
                }
                m_mediaPlayer.stop();
                m_mediaPlayer.setSource(QUrl::fromLocalFile(audioFile));
                m_mediaPlayer.play();
                setSpeaking(true);
                setStatus("Speaking locally — no cloud audio service.");
            });
    connect(&m_tts, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                if (m_busy) {
                    setBusy(false);
                    setStatus("sherpa-onnx TTS could not start. Check its executable and DLLs.");
                }
            });
    setStatus("Select local engines in Settings, then start the brain.");
}

EmberBackend::~EmberBackend() {
    cancelWork();
    m_healthTimer.stop();
    if (m_brain.state() != QProcess::NotRunning) {
        m_brain.kill();
        m_brain.waitForFinished(1500);
    }
}

QString EmberBackend::configPath(const QString &key) const {
    if (defaultPath(key).isEmpty()) return {};
    return m_settings.value("paths/" + key, defaultPath(key)).toString();
}
QString EmberBackend::pathFor(const QString &key) const {
    return configPath(key);
}
void EmberBackend::choosePath(const QString &key) {
    if (defaultPath(key).isEmpty()) return;
    QString chosen;
    if (key == "ttsDir") {
        chosen = QFileDialog::getExistingDirectory(nullptr, "Select Kokoro model folder",
                                                   configPath(key));
    } else {
        const bool executable = key.endsWith("Exe");
        const QString filter = executable ? "Windows executable (*.exe)" :
            key == "brainModel" ? "GGUF weights (*.gguf)" :
            key == "asrModel" ? "Whisper weights (*.bin)" : "All files (*)";
        chosen = QFileDialog::getOpenFileName(nullptr, "Select local file",
                                               configPath(key), filter);
    }
    if (chosen.isEmpty()) return;
    m_settings.setValue("paths/" + key, chosen);
    m_settings.sync();
    ++m_configurationRevision;
    emit configurationChanged();
    setStatus("Local path updated. Restart the brain if you changed its model.");
}
bool EmberBackend::isRegularFile(const QString &path) const {
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isReadable();
}
QString EmberBackend::workFile(const QString &name) const {
    return m_work && m_work->isValid() ? m_work->filePath(name) : QString();
}
void EmberBackend::storeConfig() {
    m_settings.setValue("persona", m_persona);
    m_settings.setValue("voiceId", m_voiceId);
    m_settings.sync();
    ++m_configurationRevision;
    emit configurationChanged();
}
void EmberBackend::setPersona(const QString &persona) {
    if (persona == m_persona || persona.trimmed().isEmpty() || persona.size() > 4096) return;
    m_persona = persona;
    storeConfig();
}
void EmberBackend::setVoiceId(int index) {
    if (index < 0 || index > 100 || index == m_voiceId) return;
    m_voiceId = index;
    storeConfig();
}
void EmberBackend::setStatus(const QString &text) {
    if (m_status == text) return;
    m_status = text;
    emit statusChanged();
}
void EmberBackend::setBusy(bool value) {
    if (m_busy == value) return;
    m_busy = value;
    emit busyChanged();
}
void EmberBackend::setListening(bool value) {
    if (m_listening == value) return;
    m_listening = value;
    emit listeningChanged();
}
void EmberBackend::setSpeaking(bool value) {
    if (m_speaking == value) return;
    m_speaking = value;
    emit speakingChanged();
}
void EmberBackend::setBrainReady(bool value) {
    if (m_brainReady == value) return;
    m_brainReady = value;
    emit brainReadyChanged();
}
void EmberBackend::appendMessage(const QString &role, const QString &text) {
    m_messages.append(QVariantMap{{"role", role}, {"body", text}});
    // Only current-session, bounded transcript. Never silently save to disk.
    while (m_messages.size() > 24) m_messages.removeFirst();
    emit messagesChanged();
}

void EmberBackend::startBrain() {
    if (m_brain.state() != QProcess::NotRunning) {
        setStatus(m_brainReady ? "Local brain is already running." : "The model is still loading.");
        return;
    }
    const QString binary = configPath("brainExe");
    const QString model = configPath("brainModel");
    if (!isRegularFile(binary) || !isRegularFile(model)) {
        setStatus("Configure a valid llama-server.exe and GGUF model in Settings.");
        return;
    }
    m_brain.setWorkingDirectory(QFileInfo(binary).absolutePath());
    m_brain.setProgram(binary);
    m_brain.setArguments({"--model", model, "--alias", "ember",
                          "--host", "127.0.0.1", "--port", "43821",
                          "--ctx-size", "4096", "--n-gpu-layers", "99",
                          "--parallel", "1"});
    m_brain.start();
    m_brainElapsed.start();
    m_healthTimer.start();
    setStatus("Loading GGUF into local inference engine…");
}
void EmberBackend::pollBrain() {
    if (m_brainReady || m_brain.state() == QProcess::NotRunning) return;
    if (m_brainElapsed.elapsed() > 90000) {
        m_healthTimer.stop();
        setStatus("Model load exceeded 90 seconds; inspect your GGUF/GPU configuration.");
        return;
    }
    const QString url = "http://127.0.0.1:43821/health";
    if (!EmberCore::allowedLocalEndpoint(url)) return;
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(1000);
    QNetworkReply *check = m_network.get(request);
    connect(check, &QNetworkReply::finished, this, [this, check]() {
        const bool ok = check->error() == QNetworkReply::NoError &&
            check->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        check->deleteLater();
        if (ok && m_brain.state() == QProcess::Running) {
            m_healthTimer.stop();
            setBrainReady(true);
            setStatus("Local brain online · RTX acceleration requested.");
        }
    });
}
void EmberBackend::stopBrain() {
    cancelWork();
    m_healthTimer.stop();
    setBrainReady(false);
    if (m_brain.state() != QProcess::NotRunning) {
        m_brain.kill();
        m_brain.waitForFinished(1000);
    }
    setStatus("Local brain stopped. No inference process remains active.");
}
void EmberBackend::submitText(const QString &text) {
    submit(text, false);
}
void EmberBackend::submit(const QString &raw, bool voice) {
    const QString text = raw.trimmed();
    if (!m_brainReady) {
        setStatus("Start the local brain before sending a message.");
        return;
    }
    if (m_busy || m_listening || text.isEmpty() || text.size() > 4096) {
        if (text.size() > 4096) setStatus("Message too long; limit is 4,096 characters.");
        return;
    }
    m_mediaPlayer.stop();
    setSpeaking(false);
    m_pendingUser = text;
    m_voicePending = voice;
    appendMessage("you", text);
    setBusy(true);
    setStatus(voice ? "Thinking about what you said…" : "Generating a local response…");
    const QJsonObject payload = EmberCore::chatRequest(m_persona, m_history, text);
    const QString url = "http://127.0.0.1:43821/v1/chat/completions";
    if (!EmberCore::allowedLocalEndpoint(url)) {
        setBusy(false);
        setStatus("The local engine endpoint is invalid.");
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(120000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_reply != reply) { reply->deleteLater(); return; }
        m_reply = nullptr;
        const QByteArray data = reply->readAll();
        const auto failure = reply->error();
        reply->deleteLater();
        if (failure != QNetworkReply::NoError || data.size() > 256 * 1024) {
            m_voicePending = false;
            setBusy(false);
            setStatus("The local LLM request failed or returned an oversized reply.");
            return;
        }
        QJsonParseError error{};
        const auto response = QJsonDocument::fromJson(data, &error);
        const auto choices = response.object().value("choices").toArray();
        const QString answer = choices.isEmpty() ? QString() :
            choices.first().toObject().value("message").toObject().value("content").toString().trimmed();
        if (error.error != QJsonParseError::NoError || answer.isEmpty() || answer.size() > 16384) {
            m_voicePending = false;
            setBusy(false);
            setStatus("The local model returned an invalid or empty response.");
            return;
        }
        m_lastReply = answer;
        emit lastReplyChanged();
        m_history.append(EmberCore::Turn{m_pendingUser, answer});
        while (m_history.size() > 8) m_history.removeFirst();
        appendMessage("ember", answer);
        const bool shouldSpeak = m_voicePending;
        m_voicePending = false;
        setBusy(false);
        if (shouldSpeak) synthesize(answer);
        else setStatus("Response complete · local inference.");
    });
}

void EmberBackend::releaseCapture() {
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }
    if (m_audioBuffer.isOpen()) m_audioBuffer.close();
    setListening(false);
}
void EmberBackend::startRecording() {
    if (!m_brainReady || m_busy || m_listening) {
        setStatus("The local brain must be online and idle before recording.");
        return;
    }
    if (!m_work || !m_work->isValid()) {
        setStatus("Could not create the private work folder in this installation.");
        return;
    }
    const QAudioDevice input = QMediaDevices::defaultAudioInput();
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (input.isNull()) {
        setStatus("No microphone is configured in Windows.");
        return;
    }
    m_captureFormat = input.isFormatSupported(format) ? format : input.preferredFormat();
    if (!m_captureFormat.isValid() || m_captureFormat.sampleRate() < 8000 ||
        m_captureFormat.sampleRate() > 96000 || m_captureFormat.channelCount() > 8 ||
        m_captureFormat.sampleFormat() == QAudioFormat::Unknown) {
        setStatus("Unsupported microphone format; choose a different Windows input device.");
        return;
    }
    m_mediaPlayer.stop();
    setSpeaking(false);
    m_pcm.clear();
    m_audioBuffer.setBuffer(&m_pcm);
    m_audioBuffer.open(QIODevice::WriteOnly);
    m_audioSource = std::make_unique<QAudioSource>(input, m_captureFormat);
    m_audioSource->start(&m_audioBuffer);
    if (m_audioSource->error() != QAudio::NoError) {
        releaseCapture();
        setStatus("Microphone capture could not start. Check Windows privacy and input device.");
        return;
    }
    setListening(true);
    setStatus("Recording locally · press Stop when you finish.");
    QTimer::singleShot(30000, this, [this]() {
        if (m_listening) stopRecording();
    });
}
void EmberBackend::stopRecording() {
    if (!m_listening) return;
    releaseCapture();
    const qint64 bytesPerSecond = qint64(m_captureFormat.sampleRate()) *
        m_captureFormat.bytesPerFrame();
    if (bytesPerSecond <= 0 || m_pcm.size() < bytesPerSecond / 10) {
        setStatus("Recording was too short. Speak for at least a moment.");
        return;
    }
    const QByteArray wav = EmberCore::whisperWav(m_pcm, m_captureFormat);
    m_pcm.clear();
    QFile file(workFile("microphone.wav"));
    if (wav.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size()) {
        setStatus("Could not prepare a local WAV file for recognition.");
        return;
    }
    file.close();
    transcribe();
}
void EmberBackend::transcribe() {
    if (!isRegularFile(configPath("asrExe")) || !isRegularFile(configPath("asrModel"))) {
        setStatus("Configure whisper-cli.exe and its model in Settings.");
        return;
    }
    QFile::remove(workFile("recognition.txt"));
    m_whisper.setWorkingDirectory(QFileInfo(configPath("asrExe")).absolutePath());
    m_whisper.setProgram(configPath("asrExe"));
    m_whisper.setArguments({"-m", configPath("asrModel"), "-f", workFile("microphone.wav"),
                            "-otxt", "-of", workFile("recognition"), "-np"});
    setBusy(true);
    setStatus("Transcribing speech fully offline…");
    m_whisper.start();
}

void EmberBackend::synthesize(const QString &text) {
    const QString root = configPath("ttsDir");
    const QDir folder(root);
    const QString model = folder.filePath("model.onnx");
    const QString voices = folder.filePath("voices.bin");
    const QString tokens = folder.filePath("tokens.txt");
    const QString lexicon = folder.filePath("lexicon-en-us.txt");
    if (!isRegularFile(configPath("ttsExe")) || !isRegularFile(model) ||
        !isRegularFile(voices) || !isRegularFile(tokens) || !isRegularFile(lexicon)) {
        setStatus("Text is ready. Configure sherpa-onnx and all Kokoro model files to hear it.");
        return;
    }
    QFile::remove(workFile("voice.wav"));
    m_tts.setWorkingDirectory(QFileInfo(configPath("ttsExe")).absolutePath());
    m_tts.setProgram(configPath("ttsExe"));
    m_tts.setArguments({"--kokoro-model=" + model,
                        "--kokoro-voices=" + voices,
                        "--kokoro-tokens=" + tokens,
                        "--kokoro-lexicon=" + lexicon,
                        "--num-threads=2",
                        "--sid=" + QString::number(m_voiceId),
                        "--output-filename=" + workFile("voice.wav"),
                        text.left(700)});
    setBusy(true);
    setStatus("Synthesizing speech locally with Kokoro…");
    m_tts.start();
}
void EmberBackend::testVoice() {
    if (m_busy || m_listening) return;
    m_mediaPlayer.stop();
    setSpeaking(false);
    synthesize("Hello. I'm Ember, your local voice assistant. Let's make something interesting together.");
}
void EmberBackend::cancelWork() {
    if (m_reply) {
        auto reply = m_reply;
        m_reply = nullptr;
        reply->abort();
    }
    if (m_whisper.state() != QProcess::NotRunning) m_whisper.kill();
    if (m_tts.state() != QProcess::NotRunning) m_tts.kill();
    releaseCapture();
    m_mediaPlayer.stop();
    setSpeaking(false);
    m_voicePending = false;
    setBusy(false);
    setStatus("Cancelled. No microphone capture or pending reply remains.");
}
void EmberBackend::clearConversation() {
    cancelWork();
    m_history.clear();
    m_messages.clear();
    m_pendingUser.clear();
    m_lastReply.clear();
    m_transcript.clear();
    emit messagesChanged();
    emit lastReplyChanged();
    emit transcriptChanged();
    setStatus("New private session. Conversation memory was cleared.");
}
