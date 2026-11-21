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
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSaveFile>
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
    // Ignore Windows/system proxies for both local inference services.
    // Otherwise an environment proxy could receive private conversation text.
    m_network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    m_voiceId = qBound(0, m_voiceId, 10);
    m_voiceEngine = qBound(0, m_settings.value("voice/engine", 0).toInt(), 1);
    m_localModel = m_settings.value("voice/localModel", "qwen3-tts").toString();
    m_localVoice = m_settings.value("voice/localVoice", "Vivian").toString();
    const QRegularExpression modelRx("^[A-Za-z0-9_./-]{1,80}$");
    const QRegularExpression voiceRx("^[A-Za-z0-9_-]{1,80}$");
    if (!modelRx.match(m_localModel).hasMatch()) m_localModel = "qwen3-tts";
    if (!voiceRx.match(m_localVoice).hasMatch()) m_localVoice = "Vivian";
    // Stable, opaque Qt device identifiers; never persist the numeric list position.
    m_inputId = QByteArray::fromBase64(m_settings.value("audio/inputId").toByteArray());
    m_outputId = QByteArray::fromBase64(m_settings.value("audio/outputId").toByteArray());
    if (m_inputId.size() > 1024) m_inputId.clear();
    if (m_outputId.size() > 1024) m_outputId.clear();
    connect(&m_mediaDevices, &QMediaDevices::audioInputsChanged,
            this, &EmberBackend::onInputInventoryChanged);
    connect(&m_mediaDevices, &QMediaDevices::audioOutputsChanged,
            this, &EmberBackend::onOutputInventoryChanged);
    onInputInventoryChanged();
    onOutputInventoryChanged();
    m_captureWatchdog.setInterval(100);
    connect(&m_captureWatchdog, &QTimer::timeout, this, [this] {
        if (!m_listening) return;
        if (m_audioSource && m_audioSource->error() != QAudio::NoError) {
            releaseCapture();
            m_pcm.clear();
            setStatus("Microphone stopped unexpectedly. Check device and Windows permissions.");
        } else if (m_pcm.size() > 30 * 1024 * 1024) {
            stopRecording();  // bounded memory even for 96-kHz multichannel inputs
        }
    });
    m_mediaPlayer.setAudioOutput(&m_audioOutput);
    m_audioOutput.setVolume(0.85);
    // Do not activate a stale/disconnected output device.
    const auto output = QMediaDevices::audioOutputs();
    for (const auto &device : output) {
        if (!m_outputId.isEmpty() && device.id() == m_outputId) {
            m_audioOutput.setDevice(device);
            break;
        }
    }
    connect(&m_mediaPlayer, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (state == QMediaPlayer::StoppedState && m_speaking) {
                    setSpeaking(false);
                    if (m_speechSequence && !m_voiceQueue.isEmpty()) {
                        // Defer the next TTS command so Qt can finish this
                        // media-state transition before reusing voice.wav.
                        QTimer::singleShot(0, this, [this] {
                            if (m_speechSequence && !m_busy && !m_speaking)
                                playNextSpeechChunk();
                        });
                    } else if (m_speechSequence) {
                        m_speechSequence = false;
                        setStatus("Finished speaking the complete reply.");
                    } else {
                        setStatus("Ready for your next question.");
                    }
                }
            });
    connect(&m_mediaPlayer, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &) {
                if (error != QMediaPlayer::NoError) {
                    m_speechSequence = false;
                    m_voiceQueue.clear();
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
                m_asrLastMs = m_asrElapsed.isValid() ? m_asrElapsed.elapsed() : -1;
                emit voiceTimingsChanged();
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
                if (!m_cliTtsActive) return; // a cancelled CLI may finish during an API request
                m_cliTtsActive = false;
                if (!m_busy) return;
                setBusy(false);
                if (how != QProcess::NormalExit || code != 0 ||
                    QFileInfo(workFile("voice.wav")).size() < 44) {
                    m_speechSequence = false;
                    m_voiceQueue.clear();
                    setStatus("Voice synthesis failed. Check the Kokoro files and voice ID.");
                    return;
                }
                m_ttsLastMs = m_ttsElapsed.isValid() ? m_ttsElapsed.elapsed() : -1;
                emit voiceTimingsChanged();
                playSynthesizedFile();
            });
    connect(&m_tts, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                if (m_busy && m_cliTtsActive) {
                    m_cliTtsActive = false;
                    m_speechSequence = false;
                    m_voiceQueue.clear();
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
    if (index < 0 || index > 10 || index == m_voiceId) return;
    m_voiceId = index;
    storeConfig();
}
void EmberBackend::setVoiceEngine(int index) {
    if (index < 0 || index > 1 || m_voiceEngine == index) return;
    if (m_busy || m_speaking || m_listening) cancelWork();
    m_voiceEngine = index;
    m_settings.setValue("voice/engine", index);
    m_settings.sync();
    ++m_configurationRevision;
    emit configurationChanged();
    setStatus(index == 0
        ? "Offline Kokoro selected. The next reply uses installed model files."
        : "Local speech service selected. Start a compatible server on 127.0.0.1:43822.");
}
void EmberBackend::setLocalVoice(const QString &value) {
    const QRegularExpression safe("^[A-Za-z0-9_-]{1,80}$");
    if (!safe.match(value).hasMatch() || m_localVoice == value) return;
    m_localVoice = value;
    m_settings.setValue("voice/localVoice", value);
    storeConfig();
}
void EmberBackend::setLocalModel(const QString &value) {
    const QRegularExpression safe("^[A-Za-z0-9_./-]{1,80}$");
    if (!safe.match(value).hasMatch() || m_localModel == value) return;
    m_localModel = value;
    m_settings.setValue("voice/localModel", value);
    storeConfig();
}

// The first entry uses the current Windows default rather than persisting
// a device ID; specific entries survive device reorder and app restarts.
QVariantList EmberBackend::audioInputs() const {
    QVariantList result;
    const QAudioDevice selected = QMediaDevices::defaultAudioInput();
    result.append(QVariantMap{{"name", selected.isNull()
        ? QString("System default (no microphone)") :
          QString("System default — %1").arg(selected.description())}});
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        result.append(QVariantMap{{"name", device.description()}});
    }
    return result;
}
QVariantList EmberBackend::audioOutputs() const {
    QVariantList result;
    const QAudioDevice selected = QMediaDevices::defaultAudioOutput();
    result.append(QVariantMap{{"name", selected.isNull()
        ? QString("System default (no speaker)") :
          QString("System default — %1").arg(selected.description())}});
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        result.append(QVariantMap{{"name", device.description()}});
    }
    return result;
}
int EmberBackend::selectedInputIndex() const {
    const auto devices = QMediaDevices::audioInputs();
    for (qsizetype i = 0; i < devices.size(); ++i)
        if (!m_inputId.isEmpty() && m_inputId == devices.at(i).id()) return int(i) + 1;
    return 0;
}
int EmberBackend::selectedOutputIndex() const {
    const auto devices = QMediaDevices::audioOutputs();
    for (qsizetype i = 0; i < devices.size(); ++i)
        if (!m_outputId.isEmpty() && m_outputId == devices.at(i).id()) return int(i) + 1;
    return 0;
}
QAudioDevice EmberBackend::activeInput() const {
    if (m_inputId.isEmpty()) return QMediaDevices::defaultAudioInput();
    for (const auto &device : QMediaDevices::audioInputs()) {
        if (device.id() == m_inputId) return device;
    }
    return QAudioDevice();
}
void EmberBackend::setInputDeviceIndex(int index) {
    const auto devices = QMediaDevices::audioInputs();
    if (index < 0 || index > devices.size()) return;
    const QByteArray selected = index == 0 ? QByteArray() : devices.at(index - 1).id();
    if (selected == m_inputId) return;
    if (m_listening) {
        releaseCapture();
        m_pcm.clear();
    }
    m_inputId = selected;
    if (m_inputId.isEmpty()) m_settings.remove("audio/inputId");
    else m_settings.setValue("audio/inputId", m_inputId.toBase64());
    m_settings.sync();
    emit audioDevicesChanged();
    setStatus("Microphone selection saved. The next recording uses this device.");
}
void EmberBackend::setOutputDeviceIndex(int index) {
    const auto devices = QMediaDevices::audioOutputs();
    if (index < 0 || index > devices.size()) return;
    const QByteArray selected = index == 0 ? QByteArray() : devices.at(index - 1).id();
    if (selected == m_outputId) return;
    m_mediaPlayer.stop();
    setSpeaking(false);
    m_outputId = selected;
    if (m_outputId.isEmpty()) m_settings.remove("audio/outputId");
    else m_settings.setValue("audio/outputId", m_outputId.toBase64());
    m_settings.sync();
    const QAudioDevice target = index == 0
        ? QMediaDevices::defaultAudioOutput() : devices.at(index - 1);
    if (!target.isNull()) m_audioOutput.setDevice(target);
    emit audioDevicesChanged();
    setStatus("Speaker selection saved. The next reply uses this output.");
}
void EmberBackend::onInputInventoryChanged() {
    const auto devices = QMediaDevices::audioInputs();
    bool selectedExists = m_inputId.isEmpty();
    bool recordingDeviceExists = !m_listening;
    for (const auto &device : devices) {
        if (device.id() == m_inputId) selectedExists = true;
        if (m_listening && device.id() == m_captureDeviceId)
            recordingDeviceExists = true;
    }
    if (!recordingDeviceExists) {
        releaseCapture();
        m_pcm.clear();
        setStatus("Selected microphone disconnected; recording was discarded.");
    }
    if (!selectedExists) {
        m_inputId.clear();
        m_settings.remove("audio/inputId");
        m_settings.sync();
        setStatus("Saved microphone disconnected; using Windows default.");
    }
    emit audioDevicesChanged();
}
void EmberBackend::onOutputInventoryChanged() {
    const auto devices = QMediaDevices::audioOutputs();
    bool selectedExists = m_outputId.isEmpty();
    for (const auto &device : devices)
        if (device.id() == m_outputId) selectedExists = true;
    if (!selectedExists) {
        m_speechSequence = false;
        m_voiceQueue.clear();
        m_mediaPlayer.stop();
        setSpeaking(false);
        m_outputId.clear();
        m_settings.remove("audio/outputId");
        m_settings.sync();
        const auto fallback = QMediaDevices::defaultAudioOutput();
        if (!fallback.isNull()) m_audioOutput.setDevice(fallback);
        setStatus("Saved speaker disconnected; using Windows default.");
    }
    emit audioDevicesChanged();
}
QString EmberBackend::voiceTimings() const {
    auto display = [](qint64 value) {
        return value < 0 ? QString("not measured") :
            QString("%1 ms").arg(value);
    };
    return QString("Last successful stages · STT %1   /   LLM %2   /   TTS chunk %3")
        .arg(display(m_asrLastMs), display(m_llmLastMs), display(m_ttsLastMs));
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
    ++m_brainSession; // invalidate late HTTP health checks from earlier runs
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
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    const quint64 session = m_brainSession;
    QNetworkReply *check = m_network.get(request);
    connect(check, &QNetworkReply::finished, this, [this, check, session]() {
        const bool ok = check->error() == QNetworkReply::NoError &&
            check->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        check->deleteLater();
        if (ok && session == m_brainSession &&
            m_brain.state() == QProcess::Running) {
            m_healthTimer.stop();
            setBrainReady(true);
            setStatus("Local brain online · RTX acceleration requested.");
        }
    });
}
void EmberBackend::stopBrain() {
    ++m_brainSession;
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
    m_speechSequence = false;
    m_voiceQueue.clear();
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
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_llmElapsed.start();
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
        m_llmLastMs = m_llmElapsed.isValid() ? m_llmElapsed.elapsed() : -1;
        emit voiceTimingsChanged();
        m_lastReply = answer;
        emit lastReplyChanged();
        m_history.append(EmberCore::Turn{m_pendingUser, answer});
        while (m_history.size() > 8) m_history.removeFirst();
        appendMessage("ember", answer);
        const bool shouldSpeak = m_voicePending;
        m_voicePending = false;
        setBusy(false);
        if (shouldSpeak) beginSpeech(answer);
        else setStatus("Response complete · local inference.");
    });
}

void EmberBackend::releaseCapture() {
    m_captureWatchdog.stop();
    m_captureDeviceId.clear();
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
    const QAudioDevice input = activeInput();
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (input.isNull()) {
        setStatus("The selected microphone is missing. Choose another device in Settings.");
        return;
    }
    m_captureFormat = input.isFormatSupported(format) ? format : input.preferredFormat();
    if (!m_captureFormat.isValid() || m_captureFormat.sampleRate() < 8000 ||
        m_captureFormat.sampleRate() > 96000 || m_captureFormat.channelCount() > 8 ||
        m_captureFormat.sampleFormat() == QAudioFormat::Unknown) {
        setStatus("Unsupported microphone format; choose a different Windows input device.");
        return;
    }
    m_speechSequence = false;
    m_voiceQueue.clear();
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
    m_captureDeviceId = input.id();
    setListening(true);
    m_captureWatchdog.start();
    setStatus("Recording from selected microphone · press Stop when you finish.");
    const quint64 session = ++m_captureSession;
    QTimer::singleShot(30000, this, [this, session]() {
        if (m_listening && m_captureSession == session) stopRecording();
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
    m_asrElapsed.start();
    setStatus("Transcribing speech fully offline…");
    m_whisper.start();
}

void EmberBackend::beginSpeech(const QString &text) {
    m_voiceQueue = EmberCore::speechChunks(text);
    if (m_voiceQueue.isEmpty()) {
        setStatus("No valid text available for the local speech engine.");
        return;
    }
    m_speechSequence = true;
    playNextSpeechChunk();
}
void EmberBackend::playNextSpeechChunk() {
    if (!m_speechSequence || m_voiceQueue.isEmpty()) return;
    const QString next = m_voiceQueue.takeFirst();
    synthesize(next);
}
void EmberBackend::synthesize(const QString &text) {
    if (m_voiceEngine == 1) {
        synthesizeViaLocalServer(text);
        return;
    }
    if (m_tts.state() != QProcess::NotRunning) {
        m_speechSequence = false;
        m_voiceQueue.clear();
        setStatus("Previous Kokoro process is stopping. Try again shortly.");
        return;
    }
    const QString root = configPath("ttsDir");
    const QDir folder(root);
    const QString model = folder.filePath("model.onnx");
    const QString voices = folder.filePath("voices.bin");
    const QString tokens = folder.filePath("tokens.txt");
    const QString dataDir = folder.filePath("espeak-ng-data");
    if (!isRegularFile(configPath("ttsExe")) || !isRegularFile(model) ||
        !isRegularFile(voices) || !isRegularFile(tokens) || !QFileInfo(dataDir).isDir()) {
        m_speechSequence = false;
        m_voiceQueue.clear();
        setStatus("Configure sherpa-onnx and the complete Kokoro v0.19 pack, including espeak-ng-data.");
        return;
    }
    // Release Windows' previous WAV file handle before regenerating a
    // later spoken segment at the same private path.
    m_mediaPlayer.setSource(QUrl());
    QFile::remove(workFile("voice.wav"));
    m_tts.setWorkingDirectory(QFileInfo(configPath("ttsExe")).absolutePath());
    m_tts.setProgram(configPath("ttsExe"));
    m_tts.setArguments({"--kokoro-model=" + model,
                        "--kokoro-voices=" + voices,
                        "--kokoro-tokens=" + tokens,
                        "--kokoro-data-dir=" + dataDir,
                        "--num-threads=2",
                        "--sid=" + QString::number(m_voiceId),
                        "--output-filename=" + workFile("voice.wav"),
                        text.left(700)});
    setBusy(true);
    m_ttsElapsed.start();
    m_cliTtsActive = true;
    setStatus("Synthesizing speech locally with Kokoro…");
    m_tts.start();
}
void EmberBackend::playSynthesizedFile() {
    m_mediaPlayer.stop();
    m_mediaPlayer.setSource(QUrl());
    m_mediaPlayer.setSource(QUrl::fromLocalFile(workFile("voice.wav")));
    m_mediaPlayer.play();
    setSpeaking(true);
    setStatus("Speaking locally — no cloud audio service.");
}
void EmberBackend::synthesizeViaLocalServer(const QString &text) {
    constexpr qsizetype maxWavBytes = 8 * 1024 * 1024;
    const QString url = "http://127.0.0.1:43822/v1/audio/speech";
    if (!EmberCore::allowedLocalSpeechEndpoint(url) || !m_work || !m_work->isValid()) {
        m_speechSequence = false;
        m_voiceQueue.clear();
        setStatus("Invalid private work folder or local speech service endpoint.");
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(90000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    m_mediaPlayer.setSource(QUrl());
    QFile::remove(workFile("voice.wav"));
    QNetworkReply *reply = m_network.post(
        request, QJsonDocument(EmberCore::speechRequest(
            m_localModel, m_localVoice, text)).toJson(QJsonDocument::Compact));
    m_speechReply = reply;
    setBusy(true);
    m_ttsElapsed.start();
    setStatus("Synthesizing with local high-quality TTS service…");
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 received, qint64) {
                if (m_speechReply == reply && received > maxWavBytes) reply->abort();
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_speechReply != reply) {
            reply->deleteLater();
            return;  // cancelled: never play outdated or crossed-over audio
        }
        m_speechReply = nullptr;
        const bool ok = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
            reply->bytesAvailable() <= maxWavBytes;
        const QByteArray wav = ok ? reply->readAll() : QByteArray();
        reply->deleteLater();
        if (!ok || !EmberCore::validSpeechWav(wav)) {
            m_speechSequence = false;
            m_voiceQueue.clear();
            setBusy(false);
            setStatus("Local TTS is offline, returned unsupported WAV or exceeded the 8 MiB limit.");
            return;
        }
        QSaveFile output(workFile("voice.wav"));
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(wav) != wav.size() || !output.commit()) {
            m_speechSequence = false;
            m_voiceQueue.clear();
            setBusy(false);
            setStatus("Could not save temporary local speech for playback.");
            return;
        }
        m_ttsLastMs = m_ttsElapsed.isValid() ? m_ttsElapsed.elapsed() : -1;
        emit voiceTimingsChanged();
        setBusy(false);
        playSynthesizedFile();
    });
}
void EmberBackend::testVoice() {
    if (m_busy || m_listening) return;
    m_speechSequence = false;
    m_voiceQueue.clear();
    m_mediaPlayer.stop();
    setSpeaking(false);
    beginSpeech("Hello. I'm Ember, your local voice assistant. Let's make something interesting together.");
}
void EmberBackend::cancelWork() {
    m_speechSequence = false;
    m_voiceQueue.clear();
    if (m_reply) {
        auto reply = m_reply;
        m_reply = nullptr;
        reply->abort();
    }
    if (m_speechReply) {
        auto reply = m_speechReply;
        m_speechReply = nullptr;
        reply->abort();
    }
    m_cliTtsActive = false;
    if (m_whisper.state() != QProcess::NotRunning) m_whisper.kill();
    if (m_tts.state() != QProcess::NotRunning) m_tts.kill();
    releaseCapture();
    m_mediaPlayer.stop();
    m_mediaPlayer.setSource(QUrl());
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
