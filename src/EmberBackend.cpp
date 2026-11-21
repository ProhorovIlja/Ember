#include "EmberBackend.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
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
void cleanupOldVoiceScratch() {
    const QDir cache(QDir(appFolder()).filePath("cache"));
    if (!cache.exists()) return;
    const QDateTime now = QDateTime::currentDateTime();
    for (const QFileInfo &folder : cache.entryInfoList(
             {"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (!EmberCore::safeScratchSessionName(folder.fileName()) ||
            folder.lastModified().secsTo(now) < 15 * 60) continue;
        const QString lockName = QDir(folder.absoluteFilePath()).filePath(".ember.lock");
        // Never delete an old unmarked folder; it may belong to another app
        // version or contain user files. New Ember instances own a lock.
        if (!QFileInfo::exists(lockName)) continue;
        QLockFile lock(lockName);
        lock.setStaleLockTime(60 * 1000);
        if (!lock.tryLock(0)) continue; // active Ember still owns this folder
        lock.unlock();
        QDir(folder.absoluteFilePath()).removeRecursively();
    }
}
QString defaultPath(const QString &key) {
    const QDir root(appFolder());
    if (key == "brainExe") return root.filePath("engines/llama/llama-server.exe");
    if (key == "brainModel") return root.filePath("models/llm/model.gguf");
    if (key == "asrExe") return root.filePath("engines/whisper/whisper-cli.exe");
    if (key == "asrModel") return root.filePath("models/stt/ggml-base.bin");
    if (key == "asrVadModel") return root.filePath("models/stt/ggml-silero-v6.2.0.bin");
    if (key == "ttsExe") return root.filePath("engines/sherpa/sherpa-onnx-offline-tts.exe");
    if (key == "speechExe") return root.filePath("engines/speech/speech.exe");
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
    cleanupOldVoiceScratch();
    m_work = std::make_unique<QTemporaryDir>(
        QDir(appFolder()).filePath("cache/session-XXXXXX"));
    if (m_work && m_work->isValid()) {
        m_workLock = std::make_unique<QLockFile>(m_work->filePath(".ember.lock"));
        m_workLock->setStaleLockTime(60 * 1000);
        if (!m_workLock->tryLock(0)) {
            // Fail closed on the scratch folder if ownership cannot be proven.
            m_workLock.reset();
            m_work.reset();
        }
    }
    m_store = std::make_unique<SessionStore>(
        QDir(appFolder()).filePath("data/ember-chats.sqlite"));
    m_persona = m_settings.value("persona", kSystemPrompt).toString();
    m_chatMode = qBound(0, m_settings.value("chat/mode", 0).toInt(), 3);
    m_recognitionLanguage = qBound(0,
        m_settings.value("asr/language", 0).toInt(), 2);
    m_recognitionEngine = qBound(0,
        m_settings.value("asr/engine", 0).toInt(), 2);
    m_chatRenderTimer.setInterval(40);
    m_chatRenderTimer.setSingleShot(true);
    connect(&m_chatRenderTimer, &QTimer::timeout,
            this, &EmberBackend::renderChatStream);
    m_pcmPumpTimer.setInterval(15);
    connect(&m_pcmPumpTimer, &QTimer::timeout, this, [this] {
        // A fast synthesizer may have paused the HTTP producer. Consume only
        // enough new bytes to stay within the bounded playback queue.
        if (m_speechReply && m_speechReply->bytesAvailable() > 0)
            readPcmResponse(m_speechReply);
        pumpPcmPlayback();
    });
    m_voiceId = m_settings.value("voiceId", 0).toInt();
    // Ignore Windows/system proxies for both local inference services.
    // Otherwise an environment proxy could receive private conversation text.
    m_network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    m_voiceId = qBound(0, m_voiceId, 10);
    m_voiceEngine = qBound(0, m_settings.value("voice/engine", 0).toInt(), 1);
    m_deliveryStyle = qBound(0, m_settings.value("voice/deliveryStyle", 0).toInt(), 4);
    m_outputVolume = qBound(0, m_settings.value("audio/outputVolume", 85).toInt(), 100);
    m_earlySpeechEnabled = m_settings.value("voice/earlySpeech", true).toBool();
    m_localModel = m_settings.value("voice/localModel", "qwen3-tts-0.6b").toString();
    m_localVoice = m_settings.value("voice/localVoice", "vivian").toString();
    const QRegularExpression modelRx("^[A-Za-z0-9_./-]{1,80}$");
    const QRegularExpression voiceRx("^[A-Za-z0-9_-]{1,80}$");
    if (!modelRx.match(m_localModel).hasMatch()) m_localModel = "qwen3-tts-0.6b";
    if (!voiceRx.match(m_localVoice).hasMatch()) m_localVoice = "vivian";
    const QString defaultSample =
        "Hello. I'm Ember, your local voice assistant. "
        "Let's make something interesting together.";
    m_voiceSample = m_settings.value("voice/sample", defaultSample).toString();
    if (m_voiceSample.trimmed().isEmpty() || m_voiceSample.size() > 1200)
        m_voiceSample = defaultSample;
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
            QFile::remove(m_captureFile.fileName());
            setStatus("Microphone stopped unexpectedly. Check device and Windows permissions.");
        } else if (m_captureFile.isOpen() && m_captureFile.size() >= 1024LL * 1024 * 1024) {
            // Long dictation has a disk quota, but no 30-second time cap.
            // Finalize rather than silently overwriting user audio.
            stopRecording();
        }
    });
    m_mediaPlayer.setAudioOutput(&m_audioOutput);
    m_audioOutput.setVolume(m_outputVolume / 100.0);
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
                if (state == QMediaPlayer::PlayingState && m_voiceTurnActive &&
                    m_voiceFirstPlaybackMs < 0 && m_voiceTurnElapsed.isValid()) {
                    m_voiceFirstPlaybackMs = m_voiceTurnElapsed.elapsed();
                    m_voiceTurnActive = false;
                    emit voiceTimingsChanged();
                }
                if (state == QMediaPlayer::StoppedState && m_speaking) {
                    setSpeaking(false);
                    // Release Windows' handle before reusing this slot later.
                    m_mediaPlayer.setSource(QUrl());
                    QTimer::singleShot(0, this, [this] {
                        if (m_speechSequence) playNextSpeechChunk();
                    });
                }
            });
    connect(&m_mediaPlayer, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &) {
                if (error != QMediaPlayer::NoError)
                    stopSpeechOnError("Windows could not play the synthesized audio.");
            });

    m_healthTimer.setInterval(1200);
    connect(&m_healthTimer, &QTimer::timeout, this, &EmberBackend::pollBrain);
    m_speechHealthTimer.setInterval(1300);
    connect(&m_speechHealthTimer, &QTimer::timeout, this, &EmberBackend::pollSpeechServer);
    m_speechServer.setProcessChannelMode(QProcess::MergedChannels);
    // Model download/status logs must not accumulate unboundedly in RAM and
    // speech text / log output is intentionally never archived to disk.
    connect(&m_speechServer, &QProcess::readyReadStandardOutput,
            this, [this] { m_speechServer.readAllStandardOutput(); });
    connect(&m_speechServer, &QProcess::started, this, [this] {
        emit speechServerChanged();
        setStatus("Native speech.cpp is loading; its first model run may download weights.");
    });
    connect(&m_speechServer, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        m_speechHealthTimer.stop();
        setSpeechServerReady(false);
        emit speechServerChanged();
        if (m_nativeAsrReply) {
            auto reply = m_nativeAsrReply;
            m_nativeAsrReply = nullptr;
            reply->abort();
            failTranscription("Native recognition service exited during transcription.");
        }
        if (m_speechSequence && m_voiceEngine == 1)
            stopSpeechOnError("The managed native speech engine exited during synthesis.");
        else
            setStatus("Managed native speech engine stopped.");
    });
    connect(&m_speechServer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_speechHealthTimer.stop();
        setSpeechServerReady(false);
        emit speechServerChanged();
        setStatus("speech.exe could not start. Check the Windows binary, GPU and model.");
    });
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
                if (!m_busy || !m_cliAsrActive) return;
                m_cliAsrActive = false; // never accept stale completed ASR after cancel
                if (how != QProcess::NormalExit || code != 0) {
                    failTranscription("Speech recognition failed. Check whisper.cpp and the selected model.");
                    return;
                }
                QFile file(workFile("recognition.txt"));
                if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
                    failTranscription("Speech recognition produced no readable transcript.");
                    return;
                }
                const QString part = QString::fromUtf8(file.readAll()).trimmed();
                if (!part.isEmpty()) m_transcriptionSegments.append(part);
                if (m_transcriptionOffset < m_transcriptionSize) {
                    transcribeNextChunk();
                } else {
                    completeTranscription();
                }
            });
    connect(&m_whisper, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                if (m_busy && m_cliAsrActive) {
                    failTranscription("whisper-cli did not start. Check its executable and DLL files.");
                }
            });

    connect(&m_tts, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus how) {
                if (!m_cliTtsActive) return; // a cancelled CLI may finish during an API request
                m_cliTtsActive = false;
                if (!m_busy) return;
                setBusy(false);
                if (how != QProcess::NormalExit || code != 0 || m_ttsOutputSlot < 0) {
                    stopSpeechOnError("Voice synthesis failed. Check the Kokoro files and voice ID.");
                    return;
                }
                QFile candidate(speechFile(m_ttsOutputSlot));
                if (!candidate.open(QIODevice::ReadOnly) ||
                    candidate.size() > 8 * 1024 * 1024 ||
                    !EmberCore::validSpeechWav(candidate.readAll())) {
                    candidate.close();
                    stopSpeechOnError("Kokoro returned invalid or oversized PCM16 WAV audio.");
                    return;
                }
                candidate.close();
                m_ttsLastMs = m_ttsElapsed.isValid() ? m_ttsElapsed.elapsed() : -1;
                emit voiceTimingsChanged();
                m_readySpeechSlot = m_ttsOutputSlot;
                m_ttsOutputSlot = -1;
                playNextSpeechChunk();
            });
    connect(&m_tts, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                if (m_busy && m_cliTtsActive)
                    stopSpeechOnError("sherpa-onnx TTS could not start. Check its executable and DLLs.");
            });
    if (m_store && m_store->ready()) {
        const QVariantList saved = m_store->sessions(1);
        const QString latest = saved.isEmpty() ? m_store->createSession()
            : saved.first().toMap().value("id").toString();
        if (!latest.isEmpty()) openSession(latest);
    }
    setStatus(m_store && m_store->ready()
        ? "Private local chat archive ready. Configure local engines to begin."
        : "Chat archive unavailable; this conversation remains in memory. "
          "Check the SQLite plugin and writable data folder.");
}

EmberBackend::~EmberBackend() {
    cancelWork();
    m_healthTimer.stop();
    if (m_brain.state() != QProcess::NotRunning) {
        m_brain.kill();
        m_brain.waitForFinished(1500);
    }
    m_speechHealthTimer.stop();
    if (m_speechServer.state() != QProcess::NotRunning) {
        m_speechServer.kill();
        m_speechServer.waitForFinished(1500);
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
            (key == "asrModel" || key == "asrVadModel") ? "Whisper/VAD weights (*.bin)" : "All files (*)";
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
void EmberBackend::setRecognitionLanguage(int language) {
    if (language < 0 || language > 2 || language == m_recognitionLanguage) return;
    m_recognitionLanguage = language;
    m_settings.setValue("asr/language", language);
    m_settings.sync();
    emit configurationChanged();
}
void EmberBackend::setRecognitionEngine(int index) {
    if (index < 0 || index > 2 || index == m_recognitionEngine) return;
    if (m_busy || m_speaking || m_listening ||
        m_speechServer.state() != QProcess::NotRunning) {
        setStatus("Stop the current task and native speech service before changing ASR.");
        return;
    }
    m_recognitionEngine = index;
    m_settings.setValue("asr/engine", index);
    m_settings.sync();
    emit configurationChanged();
    setStatus(index == 0 ? "Whisper offline recognition selected."
        : "Optional local Qwen3-ASR selected; start the native speech service first.");
}
void EmberBackend::setChatMode(int mode) {
    if (mode < 0 || mode > 3 || mode == m_chatMode) return;
    m_chatMode = mode;
    m_settings.setValue("chat/mode", mode);
    m_settings.sync();
    emit configurationChanged();
}
void EmberBackend::setPersona(const QString &persona) {
    if (persona == m_persona || persona.trimmed().isEmpty() || persona.size() > 4096) return;
    m_persona = persona;
    storeConfig();
}
void EmberBackend::setVoiceId(int index) {
    if (index < 0 || index > 10 || index == m_voiceId) return;
    if (m_busy || m_speaking || m_listening) {
        setStatus("Stop the current voice task before changing the speaker.");
        return;
    }
    m_voiceId = index;
    storeConfig();
}
void EmberBackend::setVoiceEngine(int index) {
    if (index < 0 || index > 1 || m_voiceEngine == index) return;
    if (m_busy || m_speaking || m_listening) cancelWork();
    if (index == 0 && m_speechServer.state() != QProcess::NotRunning)
        stopSpeechServer(); // release Vulkan/CPU resources when choosing Kokoro
    m_voiceEngine = index;
    m_settings.setValue("voice/engine", index);
    m_settings.sync();
    ++m_configurationRevision;
    emit configurationChanged();
    setStatus(index == 0
        ? "Offline Kokoro selected. The next reply uses installed model files."
        : "Local speech service selected. Start a compatible server on 127.0.0.1:43822.");
}
void EmberBackend::setEarlySpeechEnabled(bool value) {
    if (m_earlySpeechEnabled == value) return;
    if (m_busy || m_speaking || m_listening) {
        setStatus("Finish the current voice turn before changing early-speech mode.");
        return;
    }
    m_earlySpeechEnabled = value;
    m_settings.setValue("voice/earlySpeech", value);
    m_settings.sync();
    emit configurationChanged();
    setStatus(value ? "Early native speech enabled: synthesize after each complete sentence."
                    : "Early native speech disabled: use the full text answer before TTS.");
}
void EmberBackend::setDeliveryStyle(int style) {
    if (style < 0 || style > 4 || style == m_deliveryStyle) return;
    if (m_busy || m_listening || m_speaking) {
        setStatus("Finish the current voice turn before changing delivery style.");
        return;
    }
    m_deliveryStyle = style;
    m_settings.setValue("voice/deliveryStyle", style);
    m_settings.sync();
    emit configurationChanged();
    setStatus(m_localModel == "qwen3-tts-1.7b"
        ? "Native Qwen3 1.7B delivery style updated for the next sentence."
        : "Delivery style saved. Expressive controls require native Qwen3-TTS 1.7B.");
}
void EmberBackend::setOutputVolume(int percent) {
    const int safe = qBound(0, percent, 100);
    if (safe == m_outputVolume) return;
    m_outputVolume = safe;
    const qreal level = safe / 100.0;
    m_audioOutput.setVolume(level);
    if (m_pcmSink) m_pcmSink->setVolume(level);
    m_settings.setValue("audio/outputVolume", safe);
    m_settings.sync();
    emit configurationChanged();
}
void EmberBackend::setLocalVoice(const QString &value) {
    const QRegularExpression safe("^[A-Za-z0-9_-]{1,80}$");
    if (!safe.match(value).hasMatch() || m_localVoice == value) return;
    if (m_busy || m_speaking || m_listening) {
        setStatus("Finish speaking before changing the voice identity.");
        return;
    }
    m_localVoice = value;
    m_settings.setValue("voice/localVoice", value);
    storeConfig();
}
void EmberBackend::setVoiceSample(const QString &value) {
    if (value.size() > 1200 || value.trimmed().isEmpty() ||
        m_voiceSample == value) return;
    m_voiceSample = value;
    m_settings.setValue("voice/sample", m_voiceSample);
    storeConfig();
}
void EmberBackend::setLocalModel(const QString &value) {
    const QRegularExpression safe("^[A-Za-z0-9_./-]{1,80}$");
    if (!safe.match(value).hasMatch() || m_localModel == value) return;
    if (m_busy || m_speaking || m_listening ||
        m_speechServer.state() != QProcess::NotRunning) {
        setStatus("Stop the current voice model before changing its name.");
        return;
    }
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
        QFile::remove(m_captureFile.fileName());
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
    if (m_speechSequence || m_pcmSink) cancelWork();
    m_mediaPlayer.stop();
    m_mediaPlayer.setSource(QUrl());
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
        QFile::remove(m_captureFile.fileName());
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
        if (m_speechSequence || m_pcmSink) cancelWork();
        m_mediaPlayer.stop();
        m_mediaPlayer.setSource(QUrl());
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
    return QString("Last voice/chat timings · STT %1 · LLM first token %2 · "
                   "LLM complete %3 · TTS chunk %4 · mic release → Qt audio buffer %5")
        .arg(display(m_asrLastMs), display(m_llmFirstTokenMs),
             display(m_llmLastMs), display(m_ttsLastMs),
             display(m_voiceFirstPlaybackMs));
}
void EmberBackend::setStatus(const QString &text) {
    if (m_status == text) return;
    m_status = text;
    emit statusChanged();
}
void EmberBackend::setBusy(bool value) {
    // A synthesizer's own finished callback cannot declare Ember idle while
    // the LLM is still streaming text for the same spoken response.
    if (m_chatGenerating) value = true;
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
    // UI is bounded while SQLite keeps all text in this selected conversation.
    while (m_messages.size() > 300) m_messages.removeFirst();
    if (m_store && m_store->ready() &&
        EmberCore::shouldPersistConversation(m_privateMode, m_sessionId)) {
        if (!m_store->append(m_sessionId, role, text))
            setStatus("Chat archive write failed; this turn is held only in memory.");
        refreshSessions();
    }
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
                          "--ctx-size", "8192", "--n-gpu-layers", "99",
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
void EmberBackend::setSpeechServerReady(bool ready) {
    if (m_speechServerReady == ready) return;
    m_speechServerReady = ready;
    emit speechServerChanged();
}
void EmberBackend::startSpeechServer() {
    if (m_speechServer.state() != QProcess::NotRunning) {
        setStatus(m_speechServerReady ? "Native speech engine is ready."
                                    : "Native speech engine is already loading.");
        return;
    }
    const QString binary = configPath("speechExe");
    if (!isRegularFile(binary)) {
        setStatus("Choose a trusted native speech.exe in Settings before starting.");
        return;
    }
    // The user explicitly starts this optional process. speech.cpp may fetch
    // model weights on its first run; Ember never automatically starts it.
    const QRegularExpression modelRx("^[A-Za-z0-9_-]{1,80}$");
    if (m_voiceEngine == 1 && !modelRx.match(m_localModel).hasMatch()) {
        setStatus("Choose a speech.cpp catalog model name, e.g. qwen3-tts-0.6b.");
        return;
    }
    QStringList models{"serve"};
    if (m_voiceEngine == 1) models.append(m_localModel);
    if (m_recognitionEngine > 0)
        models.append(EmberCore::nativeAsrModel(m_recognitionEngine));
    if (models.size() < 2) {
        setStatus("Select a native voice or recognition model before starting speech.cpp.");
        return;
    }
    ++m_speechServerSession;
    setSpeechServerReady(false);
    m_speechServer.setWorkingDirectory(QFileInfo(binary).absolutePath());
    m_speechServer.setProgram(binary);
    models << "--host" << "127.0.0.1" << "--port" << "43822"
           << "--device" << "auto" << "--threads" << "4";
    m_speechServer.setArguments(models);
    m_speechServerElapsed.start();
    m_speechHealthTimer.start();
    m_speechServer.start();
    emit speechServerChanged();
    setStatus("Loading native local speech models. Explicit first start may download licensed weights.");
}
void EmberBackend::stopSpeechServer() {
    ++m_speechServerSession;
    m_speechHealthTimer.stop();
    setSpeechServerReady(false);
    if (m_speechSequence && m_voiceEngine == 1)
        stopSpeechOnError("Managed native voice synthesis was stopped.");
    if (m_nativeAsrReply) {
        auto reply = m_nativeAsrReply;
        m_nativeAsrReply = nullptr;
        reply->abort();
        failTranscription("Managed native ASR stopped. Temporary microphone audio removed.");
    }
    if (m_speechServer.state() != QProcess::NotRunning) {
        m_speechServer.kill();
        m_speechServer.waitForFinished(1000);
    }
    emit speechServerChanged();
    setStatus("Managed speech engine stopped; local text chat remains available.");
}
void EmberBackend::pollSpeechServer() {
    if (m_speechServer.state() == QProcess::NotRunning || m_speechServerReady)
        return;
    if (m_speechServerElapsed.isValid() && m_speechServerElapsed.elapsed() > 180000)
        setStatus("Speech model startup is taking longer than three minutes; check model download/GPU.");
    const quint64 session = m_speechServerSession;
    QNetworkRequest request{QUrl("http://127.0.0.1:43822/health")};
    request.setTransferTimeout(1000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, session] {
        const bool ok = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        if (ok && session == m_speechServerSession &&
            m_speechServer.state() == QProcess::Running) {
            m_speechHealthTimer.stop();
            setSpeechServerReady(true);
            setStatus("Native local speech model ready on 127.0.0.1:43822.");
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
        if (text.size() > 4096) setStatus("Message exceeds the 4,096-character input limit.");
        return;
    }
    // A PCM voice may still be playing after the LLM is done. Abort the
    // previous HTTP/sink state; stopping only the WAV player is insufficient.
    if (m_speaking || m_speechSequence || m_pcmSink || m_speechReply)
        cancelWork();
    m_speechSequence = false;
    m_voiceQueue.clear();
    m_voiceEarlyFailed = false;
    m_voiceEarlyActive = voice && m_earlySpeechEnabled && m_voiceEngine == 1 &&
        m_speechServerReady && m_speechServer.state() == QProcess::Running;
    m_liveSpeechCursor = 0;
    m_mediaPlayer.stop();
    m_mediaPlayer.setSource(QUrl());
    setSpeaking(false);
    m_pendingUser = text;
    m_voicePending = voice;
    appendMessage("you", text);
    setBusy(true);
    setStatus(voice ? "Reasoning in English for a spoken reply…"
                    : "Streaming a private local text reply…");

    // Writing profiles are provider-independent and completely local.
    // These select behavior; the user can still override m_persona.
    static const QString modes[] = {
        " Be a natural, conversational writing and thinking partner.",
        " Collaborate on richly written interactive fiction. Maintain character, "
        "world and scene continuity, show vivid dialogue and concrete detail; "
        "leave the user's character's decisions to the user. Follow their chosen style.",
        " Diagnose problems methodically. Ask only for critical unknown facts, "
        "give reproducible tests, distinguish evidence from guesses, "
        "and avoid destructive steps without clear explanation.",
        " Be a precise software development collaborator. Provide actionable code, "
        "tests, tradeoffs, commands and verified assumptions; avoid invented test results."
    };
    // Manual, per-conversation continuity is small, local and user-editable.
    // It survives session reloads without sending history to a cloud service.
    QString effectivePersona = m_persona.left(
        m_continuityNotes.isEmpty() ? 3600 : 1500) + modes[m_chatMode];
    if (!m_continuityNotes.isEmpty()) {
        effectivePersona +=
            "\n\nUser-maintained conversation memory (reference facts, not verified):\n" +
            m_continuityNotes.left(1800);
    }
    if (voice) {
        effectivePersona +=
            " For every voice reply use fluent conversational English only, even "
            "if the recognized input was Russian. Write naturally for listening. "
            "Put the answer's helpful point in the first complete sentence. "
            "Use conversational prose, not Markdown, code fences, or raw URLs.";
    }
    const QString recalled = m_store && m_store->ready() && !m_sessionId.isEmpty()
        ? m_store->historicalExcerpts(m_sessionId, text) : QString();
    QJsonObject payload = EmberCore::chatRequest(
        effectivePersona, m_history, text,
        voice ? 512 : (m_chatMode == 1 ? 2048 : 1400),
        m_chatMode == 1 ? 0.92 : 0.73, recalled);
    payload.insert("stream", true);
    const QString url = "http://127.0.0.1:43821/v1/chat/completions";
    if (!EmberCore::allowedLocalEndpoint(url)) {
        m_voicePending = false;
        m_voiceEarlyActive = false;
        setBusy(false);
        setStatus("Invalid local text-engine address.");
        return;
    }
    m_chatStreamBuffer.clear();
    m_chatStreamText.clear();
    m_chatStreamDone = m_chatStreamInvalid = m_chatPreviewActive = false;
    m_chatGenerating = true;
    m_chatStreamSession = m_sessionId;
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(180000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_network.post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_llmElapsed.start();
    m_llmFirstTokenMs = -1;
    emit voiceTimingsChanged();
    m_reply = reply;
    auto receive = [this, reply]() {
        if (m_reply != reply || m_chatStreamDone || m_chatStreamInvalid) return;
        const auto received = reply->readAll();
        const auto tokens = EmberCore::consumeChatStream(m_chatStreamBuffer, received);
        if (tokens.error || m_chatStreamText.size() + tokens.text.size() > 16384) {
            m_chatStreamInvalid = true;
            reply->abort();
            return;
        }
        if (!tokens.text.isEmpty()) {
            if (m_llmFirstTokenMs < 0 && m_llmElapsed.isValid()) {
                m_llmFirstTokenMs = m_llmElapsed.elapsed();
                emit voiceTimingsChanged();
            }
            m_chatStreamText += tokens.text;
            if (!m_chatRenderTimer.isActive()) m_chatRenderTimer.start();
            if (m_voiceEarlyActive && !m_voiceEarlyFailed)
                enqueueLiveSpeech(false);
        }
        if (tokens.done) m_chatStreamDone = true;
    };
    connect(reply, &QNetworkReply::readyRead, this, receive);
    connect(reply, &QNetworkReply::finished, this, [this, reply, receive] {
        if (m_reply != reply) {
            reply->deleteLater();
            return;
        }
        receive();
        m_reply = nullptr;
        const bool complete = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
            m_chatStreamDone && !m_chatStreamInvalid;
        reply->deleteLater();
        finishChatStream(!complete);
    });
}
void EmberBackend::renderChatStream() {
    if (m_chatStreamText.isEmpty()) return;
    if (m_chatStreamSession != m_sessionId) return;
    if (!m_chatPreviewActive) {
        m_messages.append(QVariantMap{{"role", "ember"},
                                      {"body", m_chatStreamText}});
        while (m_messages.size() > 300) m_messages.removeFirst();
        m_chatPreviewActive = true;
    } else if (!m_messages.isEmpty()) {
        QVariantMap latest = m_messages.last().toMap();
        if (latest.value("role").toString() == "ember") {
            latest.insert("body", m_chatStreamText);
            m_messages.last() = latest;
        }
    }
    emit messagesChanged();
}
void EmberBackend::enqueueLiveSpeech(bool final) {
    if (!m_voiceEarlyActive || m_voiceEarlyFailed) return;
    const QStringList segments = EmberCore::takeCommittedSpeech(
        m_chatStreamText, m_liveSpeechCursor, final);
    for (const QString &segment : segments) {
        if (EmberCore::mostlyCyrillic(segment)) {
            m_voiceEarlyFailed = true;
            stopSpeechOnError("English-only voice: primarily Russian text was not spoken.");
            return;
        }
        if (segment.isEmpty()) continue;
        m_voiceQueue.append(segment);
    }
    if (!m_voiceQueue.isEmpty() && !m_speechSequence) {
        m_readySpeechSlot = m_playingSpeechSlot = m_ttsOutputSlot = -1;
        m_speechSequence = true;
    }
    if (m_speechSequence) playNextSpeechChunk();
}
void EmberBackend::finishChatStream(bool interrupted) {
    m_chatRenderTimer.stop();
    m_chatGenerating = false;
    const QString response = m_chatStreamText.trimmed();
    const bool shouldSpeak = m_voicePending && !interrupted;
    m_voicePending = false;
    if (!response.isEmpty()) {
        if (interrupted) m_chatStreamText += "\n\n[Response stopped]";
        renderChatStream();
        m_lastReply = m_chatStreamText;
        emit lastReplyChanged();
        if (m_store && m_store->ready() &&
            EmberCore::shouldPersistConversation(m_privateMode, m_sessionId) &&
            m_chatStreamSession == m_sessionId) {
            if (!m_store->append(m_sessionId, "ember", m_chatStreamText))
                setStatus("Failed to save a streamed answer to local SQLite.");
            refreshSessions();
        }
        rebuildConversationHistory();
    }
    m_chatStreamBuffer.clear();
    m_chatPreviewActive = false;
    m_chatStreamDone = m_chatStreamInvalid = false;
    m_chatStreamSession.clear();
    if (m_llmElapsed.isValid() && !response.isEmpty()) {
        m_llmLastMs = m_llmElapsed.elapsed();
        emit voiceTimingsChanged();
    }
    const bool activeSynthesis = m_cliTtsActive || m_speechReply ||
        m_pcmSink || m_ttsOutputSlot >= 0;
    setBusy(activeSynthesis);
    if (interrupted && m_voiceEarlyActive && m_speechSequence) {
        stopSpeechOnError("Streamed response was interrupted; stopped partial voice output.");
    }
    if (response.isEmpty()) {
        setStatus("The local model returned no streamed text. Check its server logs.");
    } else if (shouldSpeak) {
        if (m_voiceEarlyActive) {
            if (!m_voiceEarlyFailed) enqueueLiveSpeech(true);
            else setStatus("Voice language or synthesis failed. Full text is saved locally.");
        } else {
            beginSpeech(response);
        }
    } else {
        setStatus(m_privateMode
            ? (interrupted ? "Private reply stopped; text kept in RAM only."
                           : "Private reply complete; no chat text archived.")
            : (interrupted ? "Generation interrupted; partial text saved locally."
                           : "Local streaming reply complete and archived."));
    }
    m_voiceEarlyActive = false;
}

void EmberBackend::releaseCapture() {
    const bool wasDictation = m_captureDictation;
    m_captureWatchdog.stop();
    m_captureDeviceId.clear();
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }
    if (m_captureFile.isOpen()) m_captureFile.close();
    m_captureDictation = false;
    setListening(false);
    if (wasDictation) emit dictationChanged();
}
void EmberBackend::startRecording() { startCapture(false); }
void EmberBackend::startDictation() { startCapture(true); }
void EmberBackend::stopDictation() {
    if (m_listening && m_captureDictation) stopRecording();
}
void EmberBackend::startCapture(bool dictation) {
    const bool interruptibleSpeech = m_speaking || m_speechSequence ||
        bool(m_pcmSink) || bool(m_speechReply) ||
        m_tts.state() != QProcess::NotRunning;
    const bool recognitionRunning = m_cliAsrActive || bool(m_nativeAsrReply) ||
        m_transcribingDictation || m_whisper.state() != QProcess::NotRunning;
    if ((!dictation && !m_brainReady) ||
        !EmberCore::captureAllowedWithInterruption(
            m_busy, m_listening, interruptibleSpeech, recognitionRunning)) {
        setStatus("Finish transcription or recording before starting the microphone.");
        return;
    }
    if (m_recognitionEngine == 0 &&
        (!isRegularFile(configPath("asrExe")) ||
         !isRegularFile(configPath("asrModel")))) {
        setStatus("Choose offline Whisper executable and multilingual weights in Settings.");
        return;
    }
    if (m_recognitionEngine > 0 &&
        (!m_speechServerReady || m_speechServer.state() != QProcess::Running)) {
        setStatus("Start and wait for native speech.cpp with the selected Qwen3-ASR model.");
        return;
    }
    if (!m_work || !m_work->isValid()) {
        setStatus("Could not create a temporary private microphone session folder.");
        return;
    }
    const QAudioDevice input = activeInput();
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (input.isNull()) {
        setStatus("Microphone disconnected. Choose a Windows recording device.");
        return;
    }
    m_captureFormat = input.isFormatSupported(format) ? format : input.preferredFormat();
    if (!m_captureFormat.isValid() || m_captureFormat.sampleRate() < 8000 ||
        m_captureFormat.sampleRate() > 96000 || m_captureFormat.channelCount() < 1 ||
        m_captureFormat.channelCount() > 8 ||
        m_captureFormat.sampleFormat() == QAudioFormat::Unknown) {
        setStatus("Unsupported microphone format; choose a different recording device.");
        return;
    }
    // Opening a fresh microphone turn must stop a previous streamed PCM
    // response as well as WAV playback, or the microphone can capture Ember
    // talking and stale local HTTP audio can leak into the next response.
    if (interruptibleSpeech)
        cancelWork();
    m_speechSequence = false;
    m_voiceQueue.clear();
    m_mediaPlayer.stop();
    m_mediaPlayer.setSource(QUrl());
    setSpeaking(false);
    m_captureFile.setFileName(workFile("microphone.raw"));
    if (!m_captureFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatus("Could not open temporary disk-backed microphone recording.");
        return;
    }
    m_captureDictation = dictation;
    m_audioSource = std::make_unique<QAudioSource>(input, m_captureFormat);
    m_audioSource->start(&m_captureFile);
    if (m_audioSource->error() != QAudio::NoError) {
        releaseCapture();
        setStatus("Microphone capture failed. Check Windows permissions and audio device.");
        return;
    }
    m_captureDeviceId = input.id();
    setListening(true);
    if (dictation) emit dictationChanged();
    m_captureWatchdog.start();
    setStatus(dictation
        ? "Dictation recording locally — hold to speak, release to transcribe RU/EN."
        : "Voice recording locally — stop when you finish.");
    const quint64 session = ++m_captureSession;
    if (!dictation) {
        QTimer::singleShot(30000, this, [this, session] {
            if (m_listening && !m_captureDictation && session == m_captureSession)
                stopRecording();
        });
    }
}
void EmberBackend::stopRecording() {
    if (!m_listening) return;
    const bool wasDictation = m_captureDictation;
    releaseCapture();
    const qint64 bytesPerSecond = qint64(m_captureFormat.sampleRate()) *
        m_captureFormat.bytesPerFrame();
    const QFileInfo recording(m_captureFile.fileName());
    if (bytesPerSecond <= 0 || recording.size() < bytesPerSecond / 10) {
        QFile::remove(m_captureFile.fileName());
        setStatus("Recording was too short to recognize.");
        return;
    }
    if (!wasDictation) {
        m_voiceTurnElapsed.start();
        m_voiceTurnActive = true;
        m_voiceFirstPlaybackMs = -1;
        emit voiceTimingsChanged();
    }
    m_transcriptionSize = recording.size();
    m_transcriptionOffset = 0;
    // Bound a single 25-second segment by the Core resampler's 32 MiB
    // raw-input limit even on 8-channel/96 kHz/float capture hardware.
    const qint64 frameBytes = m_captureFormat.bytesPerFrame();
    const qint64 rawBudget = 30LL * 1024 * 1024;
    m_transcriptionChunkSize = (std::min(bytesPerSecond * 25, rawBudget) /
        frameBytes) * frameBytes;
    m_transcriptionSegments.clear();
    m_transcribingDictation = wasDictation;
    m_asrElapsed.start();
    transcribeNextChunk();
}
void EmberBackend::transcribeNextChunk() {
    if (m_transcriptionOffset >= m_transcriptionSize) {
        completeTranscription();
        return;
    }
    // Repeat half a second of preceding audio in a later dictation chunk.
    // Without overlap, Whisper can miss words straddling the exact boundary.
    const qint64 overlap = m_transcribingDictation && m_transcriptionOffset > 0
        ? qint64(m_captureFormat.sampleRate()) * m_captureFormat.bytesPerFrame() / 2
        : 0;
    const qint64 start = m_transcriptionOffset - overlap;
    const qint64 freshBytes = std::min(m_transcriptionChunkSize,
                                      m_transcriptionSize - m_transcriptionOffset);
    QFile file(m_captureFile.fileName());
    if (!file.open(QIODevice::ReadOnly) || !file.seek(start)) {
        failTranscription("Could not read the temporary local microphone recording.");
        return;
    }
    const qint64 size = freshBytes + overlap;
    const QByteArray raw = file.read(size);
    file.close();
    if (size <= 0 || raw.size() != size) {
        failTranscription("Microphone recording was truncated during transcription.");
        return;
    }
    const QByteArray wav = EmberCore::whisperWav(raw, m_captureFormat);
    QFile output(workFile("microphone.wav"));
    if (wav.isEmpty() || !output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        output.write(wav) != wav.size()) {
        output.close();
        failTranscription("Unable to resample a recorded voice segment into local PCM WAV.");
        return;
    }
    output.close();
    m_transcriptionOffset += freshBytes;
    transcribe();
}
void EmberBackend::transcribe() {
    if (m_recognitionEngine != 0) {
        transcribeViaNativeServer();
        return;
    }
    if (!isRegularFile(configPath("asrExe")) || !isRegularFile(configPath("asrModel"))) {
        failTranscription("Offline Whisper files are missing from this installation.");
        return;
    }
    QFile::remove(workFile("recognition.txt"));
    m_whisper.setWorkingDirectory(QFileInfo(configPath("asrExe")).absolutePath());
    m_whisper.setProgram(configPath("asrExe"));
    QStringList arguments{"-m", configPath("asrModel"), "-f", workFile("microphone.wav"),
                          "-l", EmberCore::recognitionLanguageTag(m_recognitionLanguage),
                          "-otxt", "-of", workFile("recognition"), "-np"};
    // Current whisper.cpp supports an optional GGML Silero VAD model.
    // The setting is opt-in by installing its tiny weights; legacy Whisper
    // engines and users without the model keep the previous invocation.
    if (isRegularFile(configPath("asrVadModel")))
        arguments << "--vad" << "-vm" << configPath("asrVadModel");
    m_whisper.setArguments(arguments);
    m_cliAsrActive = true;
    setBusy(true);
    setStatus(QString("Offline transcription · %1% completed…")
                  .arg(m_transcriptionOffset * 100 / m_transcriptionSize));
    m_whisper.start();
}
void EmberBackend::transcribeViaNativeServer() {
    const QString address = "http://127.0.0.1:43822/v1/audio/transcriptions";
    if (!EmberCore::allowedLocalAsrEndpoint(address) ||
        !m_speechServerReady || m_speechServer.state() != QProcess::Running ||
        m_nativeAsrReply) {
        failTranscription("Native local Qwen3-ASR is not ready for transcription.");
        return;
    }
    const QString wavName = workFile("microphone.wav");
    if (!m_work || !m_work->isValid() || !QFileInfo(wavName).isFile() ||
        QFileInfo(wavName).size() > 25LL * 1024 * 1024) {
        failTranscription("Temporary microphone WAV is invalid or exceeds the 25 MiB quota.");
        return;
    }
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto textField = [multipart](const QString &field, const QByteArray &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
            QString("form-data; name=\"%1\"").arg(field));
        part.setBody(value);
        multipart->append(part);
    };
    textField("model", EmberCore::nativeAsrModel(m_recognitionEngine).toUtf8());
    textField("response_format", "text");
    textField("language", EmberCore::recognitionLanguageTag(m_recognitionLanguage).toUtf8());
    const QString hints = EmberCore::compactAsrHints(m_recognitionHints);
    if (!hints.isEmpty())
        textField("prompt", hints.toUtf8()); // Qwen3-ASR uses terms as background
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        "form-data; name=\"file\"; filename=\"microphone.wav\"");
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, "audio/wav");
    auto *wav = new QFile(wavName, multipart);
    if (!wav->open(QIODevice::ReadOnly)) {
        delete multipart;
        failTranscription("Cannot open the temporary local transcription input.");
        return;
    }
    filePart.setBodyDevice(wav);
    multipart->append(filePart);
    QNetworkRequest request{QUrl(address)};
    request.setTransferTimeout(180000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_network.post(request, multipart);
    multipart->setParent(reply);
    m_nativeAsrReply = reply;
    setBusy(true);
    setStatus(QString("Native Qwen3-ASR transcribing segment %1% locally…")
        .arg(m_transcriptionOffset * 100 / std::max<qint64>(1, m_transcriptionSize)));
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 bytes, qint64) {
                if (m_nativeAsrReply == reply && bytes > 65536) reply->abort();
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_nativeAsrReply != reply) {
            reply->deleteLater();
            return; // cancelled or superseded
        }
        m_nativeAsrReply = nullptr;
        const bool valid = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
            reply->rawHeader("Content-Type").toLower().startsWith("text/plain") &&
            reply->bytesAvailable() <= 65536 &&
            reply->rawHeader("X-Speech-Stop").toLower() != "model_limit";
        const QByteArray raw = valid ? reply->readAll() : QByteArray();
        reply->deleteLater();
        if (!valid) {
            failTranscription("Native Qwen3-ASR failed, hit its token limit, or returned invalid text.");
            return;
        }
        const QString segment = QString::fromUtf8(raw).trimmed();
        if (!segment.isEmpty()) m_transcriptionSegments.append(segment);
        if (m_transcriptionOffset < m_transcriptionSize)
            transcribeNextChunk();
        else
            completeTranscription();
    });
}

void EmberBackend::failTranscription(const QString &reason) {
    m_cliAsrActive = false;
    if (m_nativeAsrReply) {
        auto reply = m_nativeAsrReply;
        m_nativeAsrReply = nullptr;
        reply->abort();
    }
    m_voiceTurnActive = false;
    m_transcribingDictation = false;
    m_transcriptionSegments.clear();
    m_transcriptionSize = m_transcriptionOffset = m_transcriptionChunkSize = 0;
    for (const auto &name : {"microphone.raw", "microphone.wav", "recognition.txt"})
        QFile::remove(workFile(name));
    setBusy(false);
    setStatus(reason);
}
void EmberBackend::completeTranscription() {
    const bool dictate = m_transcribingDictation;
    m_transcribingDictation = false;
    const QString text = EmberCore::joinTranscriptionSegments(m_transcriptionSegments);
    m_transcriptionSegments.clear();
    m_transcriptionOffset = m_transcriptionSize = 0;
    QFile::remove(workFile("microphone.raw"));
    QFile::remove(workFile("microphone.wav"));
    QFile::remove(workFile("recognition.txt"));
    m_asrLastMs = m_asrElapsed.isValid() ? m_asrElapsed.elapsed() : -1;
    emit voiceTimingsChanged();
    setBusy(false);
    if (text.isEmpty()) {
        setStatus("No speech recognized. Try again with a clearer microphone signal.");
        return;
    }
    m_transcript = text;
    emit transcriptChanged();
    if (dictate) {
        m_dictatedText = text;
        emit dictationChanged();
        emit dictationReady(text);
        setStatus("Dictation ready for editing. English/Russian text remains local.");
    } else {
        submit(text, true);
    }
}

QString EmberBackend::speechFile(int slot) const {
    return workFile(slot == 0 ? "voice-a.wav" : "voice-b.wav");
}
void EmberBackend::stopSpeechOnError(const QString &error) {
    if (m_chatGenerating && m_voiceEarlyActive) m_voiceEarlyFailed = true;
    m_speechSequence = false;
    m_voiceQueue.clear();
    m_readySpeechSlot = m_ttsOutputSlot = m_playingSpeechSlot = -1;
    m_cliTtsActive = false;
    resetPcmPlayback();
    if (m_speechReply) {
        auto reply = m_speechReply;
        m_speechReply = nullptr;
        reply->abort();
    }
    if (m_tts.state() != QProcess::NotRunning) m_tts.kill();
    m_mediaPlayer.stop();
    m_mediaPlayer.setSource(QUrl());
    QFile::remove(speechFile(0));
    QFile::remove(speechFile(1));
    setSpeaking(false);
    setBusy(false);
    setStatus(error);
}
void EmberBackend::beginSpeech(const QString &text) {
    if (EmberCore::mostlyCyrillic(text)) {
        m_voiceTurnActive = false;
        setStatus("English-only voice: the model answered mainly in Russian. "
                  "Its original text is preserved; please retry in English.");
        return;
    }
    m_voiceQueue = EmberCore::speechChunks(EmberCore::speechFriendlyText(text));
    if (m_voiceQueue.isEmpty()) {
        setStatus("No valid text available for the local speech engine.");
        return;
    }
    m_readySpeechSlot = m_playingSpeechSlot = m_ttsOutputSlot = -1;
    m_speechSequence = true;
    playNextSpeechChunk();
}
void EmberBackend::playNextSpeechChunk() {
    if (!m_speechSequence) return;
    // The current chunk can play while the opposite WAV slot is synthesized.
    // At most one prepared chunk is buffered, keeping scratch disk bounded.
    if (!m_speaking && m_readySpeechSlot >= 0)
        playSynthesizedFile();
    // One TTS request may run concurrently with a still-generating LLM turn.
    // Do not start a second synthesis until the first one has completed.
    if ((m_busy && !m_chatGenerating) || m_readySpeechSlot >= 0 ||
        m_cliTtsActive || m_speechReply || m_pcmSink) return;
    if (!m_voiceQueue.isEmpty()) {
        const QString next = m_voiceQueue.takeFirst();
        m_ttsOutputSlot = (m_playingSpeechSlot == 0 ? 1 : 0);
        synthesize(next);
    } else if (!m_speaking) {
        // All available complete sentences may have been voiced before the
        // LLM emits its next one. Keep the sequence alive until SSE [DONE].
        if (m_chatGenerating && m_voiceEarlyActive && !m_voiceEarlyFailed) {
            setStatus("Listening to Ember while the next sentence is generated…");
            return;
        }
        m_speechSequence = false;
        m_mediaPlayer.setSource(QUrl());
        QFile::remove(speechFile(0));
        QFile::remove(speechFile(1));
        setStatus("Finished speaking the complete reply. Temporary audio removed.");
    }
}
void EmberBackend::synthesize(const QString &text) {
    if (m_voiceEngine == 1) {
        // Only the explicitly started, health-verified speech.cpp server is
        // known to support chunked raw PCM. Other compatible providers keep
        // the existing validated complete-WAV request and two-slot fallback.
        if (m_speechServerReady && m_speechServer.state() == QProcess::Running)
            synthesizeStreamingPcm(text);
        else
            synthesizeViaLocalServer(text);
        return;
    }
    if (m_tts.state() != QProcess::NotRunning) {
        stopSpeechOnError("Previous Kokoro process is stopping. Try again shortly.");
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
        stopSpeechOnError("Configure sherpa-onnx and the complete Kokoro v0.19 pack, including espeak-ng-data.");
        return;
    }
    // Alternating private files allow synthesis during active playback.
    // Never overwrite the WAV currently owned by QMediaPlayer.
    QFile::remove(speechFile(m_ttsOutputSlot));
    m_tts.setWorkingDirectory(QFileInfo(configPath("ttsExe")).absolutePath());
    m_tts.setProgram(configPath("ttsExe"));
    m_tts.setArguments({"--kokoro-model=" + model,
                        "--kokoro-voices=" + voices,
                        "--kokoro-tokens=" + tokens,
                        "--kokoro-data-dir=" + dataDir,
                        "--num-threads=2",
                        "--sid=" + QString::number(m_voiceId),
                        "--output-filename=" + speechFile(m_ttsOutputSlot),
                        text.left(700)});
    setBusy(true);
    m_ttsElapsed.start();
    m_cliTtsActive = true;
    setStatus("Synthesizing speech locally with Kokoro…");
    m_tts.start();
}
void EmberBackend::playSynthesizedFile() {
    if (m_readySpeechSlot < 0) return;
    const int slot = m_readySpeechSlot;
    m_readySpeechSlot = -1;
    m_playingSpeechSlot = slot;
    m_mediaPlayer.stop();
    m_mediaPlayer.setSource(QUrl());
    m_mediaPlayer.setSource(QUrl::fromLocalFile(speechFile(slot)));
    m_mediaPlayer.play();
    if (!m_speechSequence) return; // synchronous media error may cancel playback
    setSpeaking(true);
    setStatus("Speaking locally — the next segment can prepare in parallel.");
}
void EmberBackend::resetPcmPlayback() {
    m_pcmPumpTimer.stop();
    m_pcmDevice = nullptr; // QAudioSink owns this temporary push-mode device
    if (m_pcmSink) {
        m_pcmSink->stop();
        m_pcmSink.reset();
    }
    m_pcmPending.clear();
    m_pcmTotalReceived = 0;
    m_pcmBytesSubmitted = 0;
    m_pcmDrainElapsed.invalidate();
    m_pcmSampleRate = 0;
    m_pcmHttpDone = false;
    m_pcmStarted = false;
}
void EmberBackend::synthesizeStreamingPcm(const QString &text) {
    const QString url = "http://127.0.0.1:43822/v1/audio/speech";
    if (!EmberCore::allowedLocalSpeechEndpoint(url) || m_pcmSink ||
        !m_speechServerReady || m_speechServer.state() != QProcess::Running) {
        stopSpeechOnError("The managed local PCM speech service is not ready.");
        return;
    }
    resetPcmPlayback();
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(120000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "audio/pcm");
    const QJsonObject payload = EmberCore::streamingPcmSpeechRequest(
        m_localModel, m_localVoice, text, m_deliveryStyle);
    auto *reply = m_network.post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    // Backpressure: Qt must not buffer an entire multi-minute synthesized
    // stream when the model produces audio faster than Windows consumes it.
    reply->setReadBufferSize(128 * 1024);
    m_speechReply = reply;
    setBusy(true);
    m_ttsElapsed.start();
    setStatus("Streaming private PCM16 speech directly from native Qwen3-TTS…");
    connect(reply, &QNetworkReply::readyRead,
            this, [this, reply] { readPcmResponse(reply); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_speechReply != reply) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
            m_pcmSampleRate <= 0) {
            stopSpeechOnError("Native PCM stream failed or returned invalid audio.");
            reply->deleteLater();
            return;
        }
        m_pcmHttpDone = true;
        m_ttsLastMs = m_ttsElapsed.isValid() ? m_ttsElapsed.elapsed() : -1;
        emit voiceTimingsChanged();
        readPcmResponse(reply); // remaining bytes may exceed one queue window
        if (m_speechReply != reply) {
            reply->deleteLater();
            return;
        }
        if (reply->bytesAvailable() == 0) {
            m_speechReply = nullptr;
            reply->deleteLater();
            if (m_pcmTotalReceived <= 0 || (m_pcmTotalReceived % 2)) {
                stopSpeechOnError("Native PCM stream ended with an incomplete sample.");
                return;
            }
        }
        pumpPcmPlayback();
    });
}
void EmberBackend::readPcmResponse(QNetworkReply *reply) {
    if (m_speechReply != reply) return;
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
        stopSpeechOnError("Native speech API rejected the streaming PCM request.");
        return;
    }
    if (!m_pcmSink) {
        const QByteArray contentType = reply->rawHeader("Content-Type").toLower();
        bool rateOk = false;
        const int rate = reply->rawHeader("X-Sample-Rate").trimmed().toInt(&rateOk);
        if (!contentType.startsWith("audio/pcm") || !rateOk ||
            !EmberCore::validPcmSampleRate(rate)) {
            stopSpeechOnError("Native TTS response must be mono PCM16 with a valid X-Sample-Rate.");
            return;
        }
        QAudioFormat format;
        format.setSampleRate(rate);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        const QAudioDevice selected = m_audioOutput.device();
        if (selected.isNull() || !selected.isFormatSupported(format)) {
            stopSpeechOnError("Selected Windows output does not support this PCM rate and format.");
            return;
        }
        m_pcmSampleRate = rate;
        m_pcmSink = std::make_unique<QAudioSink>(selected, format);
        m_pcmSink->setVolume(m_outputVolume / 100.0);
        m_pcmSink->setBufferSize(rate / 2); // ~250 ms mono PCM16 audio buffer
        m_pcmDevice = m_pcmSink->start();
        if (!m_pcmDevice || m_pcmSink->error() != QAudio::NoError) {
            stopSpeechOnError("Could not initialize the selected Windows streaming audio device.");
            return;
        }
        m_pcmPumpTimer.start();
    }
    constexpr qint64 maxChunkBytes = 12LL * 1024 * 1024;
    constexpr qsizetype maxPendingBytes = 768 * 1024;
    // Consume only free queue capacity. QNetworkReply's own 128 KiB read
    // limit applies TCP-level backpressure instead of aborting fast models.
    const qint64 room = maxPendingBytes - m_pcmPending.size();
    if (room <= 0) return;
    const QByteArray incoming = reply->read(
        std::min<qint64>(room, reply->bytesAvailable()));
    if (incoming.isEmpty()) return;
    if (incoming.size() > maxChunkBytes - m_pcmTotalReceived) {
        stopSpeechOnError("Native PCM stream exceeded the per-chunk duration quota.");
        return;
    }
    m_pcmTotalReceived += incoming.size();
    m_pcmPending += incoming;
    pumpPcmPlayback();
    if (m_pcmHttpDone && m_speechReply == reply && reply->bytesAvailable() == 0) {
        m_speechReply = nullptr;
        reply->deleteLater();
        if (m_pcmTotalReceived <= 0 || (m_pcmTotalReceived % 2))
            stopSpeechOnError("Native PCM stream ended with an incomplete sample.");
    }
}
void EmberBackend::pumpPcmPlayback() {
    if (!m_pcmSink || !m_pcmDevice) return;
    if (m_pcmSink->error() != QAudio::NoError &&
        m_pcmSink->error() != QAudio::UnderrunError) {
        stopSpeechOnError("The Windows PCM playback stream encountered an audio device error.");
        return;
    }
    const qint64 available = std::max<qint64>(0, m_pcmSink->bytesFree());
    // PCM16 frame boundaries may span separate HTTP readyRead notifications.
    const qint64 writeBytes = std::min<qint64>(
        available, m_pcmPending.size()) & ~qint64(1);
    if (writeBytes > 0) {
        const qint64 written = m_pcmDevice->write(m_pcmPending.constData(), writeBytes);
        if (written < 0 || (written % 2)) {
            stopSpeechOnError("Windows rejected the PCM16 audio buffer.");
            return;
        }
        if (written > 0) {
            m_pcmBytesSubmitted += written;
            m_pcmPending.remove(0, qsizetype(written));
            if (!m_pcmStarted) {
                m_pcmStarted = true;
                setSpeaking(true);
                // Approximate first-buffer-accepted time, NOT acoustic onset.
                if (m_voiceTurnActive && m_voiceFirstPlaybackMs < 0 &&
                    m_voiceTurnElapsed.isValid()) {
                    m_voiceFirstPlaybackMs = m_voiceTurnElapsed.elapsed();
                    m_voiceTurnActive = false;
                    emit voiceTimingsChanged();
                }
            }
        }
    }
    // Qt starts push mode in IdleState before it has played a single sample.
    // A response that completes in one packet must not be torn down before
    // the buffered audio has actually had time to reach the device.
    if (m_pcmHttpDone && !m_speechReply && m_pcmPending.isEmpty() &&
        m_pcmStarted && m_pcmSampleRate > 0) {
        if (!m_pcmDrainElapsed.isValid()) m_pcmDrainElapsed.start();
        const qint64 expectedUs = m_pcmBytesSubmitted * 1000000LL /
                                  (qint64(m_pcmSampleRate) * 2);
        const bool consumed = EmberCore::pcmPlaybackDrained(
            m_pcmBytesSubmitted, m_pcmSampleRate, m_pcmSink->processedUSecs());
        if (consumed && m_pcmSink->state() == QAudio::IdleState) {
            resetPcmPlayback();
            setSpeaking(false);
            setBusy(false);
            m_ttsOutputSlot = -1;
            playNextSpeechChunk();
        } else if (m_pcmDrainElapsed.elapsed() >
                   std::max<qint64>(8000, expectedUs / 1000 + 8000)) {
            // A full sentence may last far longer than eight seconds.
            // Budget the expected audio duration plus a bounded 8 s margin.
            stopSpeechOnError("Windows PCM output stalled beyond the audio-duration budget.");
        }
    }
}
void EmberBackend::synthesizeViaLocalServer(const QString &text) {
    constexpr qsizetype maxWavBytes = 8 * 1024 * 1024;
    const QString url = "http://127.0.0.1:43822/v1/audio/speech";
    if (!EmberCore::allowedLocalSpeechEndpoint(url) || !m_work || !m_work->isValid()) {
        stopSpeechOnError("Invalid private work folder or local speech service endpoint.");
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(90000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QFile::remove(speechFile(m_ttsOutputSlot));
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
            stopSpeechOnError("Local TTS is offline, returned unsupported WAV or exceeded the 8 MiB limit.");
            return;
        }
        QSaveFile output(speechFile(m_ttsOutputSlot));
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(wav) != wav.size() || !output.commit()) {
            stopSpeechOnError("Could not save temporary local speech for playback.");
            return;
        }
        m_ttsLastMs = m_ttsElapsed.isValid() ? m_ttsElapsed.elapsed() : -1;
        emit voiceTimingsChanged();
        setBusy(false);
        m_readySpeechSlot = m_ttsOutputSlot;
        m_ttsOutputSlot = -1;
        playNextSpeechChunk();
    });
}
void EmberBackend::testVoice() {
    if (m_busy || m_listening) return;
    if (m_voiceEngine == 1 &&
        m_speechServer.state() != QProcess::NotRunning && !m_speechServerReady) {
        setStatus("Native speech model is still loading; test it after Ready appears.");
        return;
    }
    m_speechSequence = false;
    m_voiceQueue.clear();
    m_mediaPlayer.stop();
    m_mediaPlayer.setSource(QUrl());
    setSpeaking(false);
    beginSpeech(m_voiceSample);
}
void EmberBackend::cancelWork() {
    m_cliAsrActive = false;
    if (m_nativeAsrReply) {
        auto reply = m_nativeAsrReply;
        m_nativeAsrReply = nullptr;
        reply->abort();
    }
    m_voiceTurnActive = false;
    m_chatGenerating = false;
    m_voiceEarlyFailed = true;
    m_speechSequence = false;
    m_voiceQueue.clear();
    m_readySpeechSlot = m_ttsOutputSlot = m_playingSpeechSlot = -1;
    resetPcmPlayback();
    if (m_reply) {
        auto reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        finishChatStream(true); // persist only text already received, not audio
    }
    if (m_speechReply) {
        auto reply = m_speechReply;
        m_speechReply = nullptr;
        reply->abort();
    }
    m_cliTtsActive = false;
    if (m_whisper.state() != QProcess::NotRunning) m_whisper.kill();
    m_transcribingDictation = false;
    m_transcriptionSegments.clear();
    m_transcriptionSize = m_transcriptionOffset = 0;
    if (!m_captureFile.fileName().isEmpty())
        QFile::remove(m_captureFile.fileName());
    if (m_tts.state() != QProcess::NotRunning) m_tts.kill();
    releaseCapture();
    m_mediaPlayer.stop();
    m_mediaPlayer.setSource(QUrl());
    QFile::remove(speechFile(0));
    QFile::remove(speechFile(1));
    setSpeaking(false);
    m_voicePending = false;
    m_voiceEarlyActive = false;
    m_liveSpeechCursor = 0;
    setBusy(false);
    setStatus("Cancelled. No microphone capture or pending reply remains.");
}
QVariantList EmberBackend::searchHistory(const QString &text) const {
    return m_store && m_store->ready() ? m_store->search(text) : QVariantList();
}
void EmberBackend::exportCurrentSession() {
    if (m_privateMode) {
        setStatus("Private conversation export disabled to prevent accidental disk writes.");
        return;
    }
    if (!m_store || !m_store->ready() || m_sessionId.isEmpty()) {
        setStatus("Local history is not available for export.");
        return;
    }
    const QString output = QFileDialog::getSaveFileName(
        nullptr, "Export this conversation",
        QDir::home().filePath("Ember-conversation.md"),
        "Markdown document (*.md)");
    if (output.isEmpty()) return;
    setStatus(m_store->exportMarkdown(m_sessionId, output)
        ? "Complete text conversation exported locally. No audio was saved."
        : "Could not export the selected conversation.");
}

void EmberBackend::exportCompactHistory() {
    if (m_privateMode || m_busy || m_listening || m_speaking ||
        !m_store || !m_store->ready()) {
        setStatus("Finish the current task and exit Private session before backing up.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        nullptr, "Save all local conversations",
        QDir::home().filePath("Ember-chats.emberbackup"),
        "Ember compressed backup (*.emberbackup)");
    if (path.isEmpty()) return;
    setStatus(m_store->exportCompactBackup(path)
        ? "Local compressed chat backup saved. Not encrypted; keep the file private."
        : "Could not create compressed chat backup. Existing chats were not changed.");
}
void EmberBackend::restoreCompactHistory() {
    if (m_privateMode || m_busy || m_listening || m_speaking ||
        !m_store || !m_store->ready()) {
        setStatus("Finish the current task and exit Private session before restoring.");
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        nullptr, "Restore a local Ember backup", QDir::homePath(),
        "Ember compressed backup (*.emberbackup)");
    if (path.isEmpty()) return;
    if (!m_store->restoreCompactBackup(path)) {
        setStatus("Backup was not restored. File may be damaged; existing chats are unchanged.");
        return;
    }
    refreshSessions();
    setStatus("Chats restored as separate copies. Existing conversations were preserved.");
}

void EmberBackend::setContinuityNotes(const QString &text) {
    if (!m_store || !m_store->ready() || m_sessionId.isEmpty()) {
        setStatus("Local story memory requires a writable SQLite conversation.");
        return;
    }
    if (text.size() > 1800) {
        setStatus("Conversation memory is limited to 1,800 characters.");
        return;
    }
    if (!m_store->setNotes(m_sessionId, text)) {
        setStatus("Could not update this conversation's local memory.");
        return;
    }
    m_continuityNotes = text.trimmed();
    emit continuityNotesChanged();
    setStatus("Saved local conversation memory. It follows only this chat.");
}
void EmberBackend::setRecognitionHints(const QString &text) {
    if (!m_store || !m_store->ready() || m_sessionId.isEmpty()) {
        setStatus("Recognition terminology requires an active private local chat.");
        return;
    }
    if (m_busy || m_listening || m_speaking) {
        setStatus("Finish the current recording before editing recognition terms.");
        return;
    }
    if (text.size() > 512) {
        setStatus("Local recognition vocabulary is limited to 512 characters.");
        return;
    }
    if (!m_store->setAsrHints(m_sessionId, text)) {
        setStatus("Could not store this conversation's private recognition terms.");
        return;
    }
    m_recognitionHints = text.trimmed();
    emit recognitionHintsChanged();
    setStatus("Recognition terms saved locally for this conversation only.");
}
void EmberBackend::refreshSessions() {
    m_sessions = m_store && m_store->ready() ? m_store->sessions() : QVariantList();
    emit sessionsChanged();
}
void EmberBackend::rebuildConversationHistory() {
    m_history.clear();
    QString pending;
    for (const auto &v : m_messages) {
        const auto item = v.toMap();
        if (item.value("role").toString() == "you") {
            pending = item.value("body").toString();
        } else if (item.value("role").toString() == "ember" && !pending.isEmpty()) {
            m_history.append(EmberCore::Turn{pending, item.value("body").toString()});
            pending.clear();
        }
    }
    while (m_history.size() > 48) m_history.removeFirst();
}
void EmberBackend::setPrivateMode(bool enabled) {
    if (enabled == m_privateMode) return;
    cancelWork();
    if (!enabled) {
        m_privateMode = false;
        emit privateModeChanged();
        // Restore an ordinary saved chat without copying private text.
        const QVariantList existing = m_store && m_store->ready()
            ? m_store->sessions(1) : QVariantList();
        const QString previous = existing.isEmpty() ? QString()
            : existing.first().toMap().value("id").toString();
        if (!previous.isEmpty()) openSession(previous);
        else clearConversation();
        setStatus("Private mode ended. Restored ordinary local conversation.");
        return;
    }
    m_privateMode = true;
    emit privateModeChanged();
    clearConversation(); // discard in-memory history; do not create SQLite chat
    setStatus("Private text chat stays in RAM, not SQLite. "
              "Microphone transcription still uses temporary local audio files.");
}
void EmberBackend::openSession(const QString &id) {
    if (!m_store || !m_store->ready() || !m_store->exists(id)) return;
    cancelWork();
    if (m_privateMode) {
        m_privateMode = false;
        emit privateModeChanged();
    }
    m_sessionId = id;
    m_continuityNotes = m_store->notes(id);
    emit continuityNotesChanged();
    m_recognitionHints = m_store->asrHints(id);
    emit recognitionHintsChanged();
    m_messages = m_store->messages(id);
    rebuildConversationHistory();
    m_pendingUser.clear();
    m_lastReply.clear();
    m_transcript.clear();
    emit messagesChanged();
    emit lastReplyChanged();
    emit transcriptChanged();
    refreshSessions();
    setStatus("Local conversation loaded. No text was uploaded.");
}
void EmberBackend::clearConversation() {
    cancelWork();
    m_sessionId = !m_privateMode && m_store && m_store->ready()
        ? m_store->createSession() : QString();
    m_history.clear();
    m_messages.clear();
    m_continuityNotes.clear();
    emit continuityNotesChanged();
    m_recognitionHints.clear();
    emit recognitionHintsChanged();
    m_pendingUser.clear();
    m_lastReply.clear();
    m_transcript.clear();
    emit messagesChanged();
    emit lastReplyChanged();
    emit transcriptChanged();
    refreshSessions();
    setStatus(m_privateMode ? "New private conversation in RAM; no chat text archived."
                            : "New conversation. Older chats remain saved locally.");
}
void EmberBackend::deleteCurrentSession() {
    if (m_privateMode) {
        clearConversation();
        setStatus("Private conversation discarded from live memory.");
        return;
    }
    if (!m_store || !m_store->ready() || m_sessionId.isEmpty()) return;
    cancelWork();
    if (!m_store->remove(m_sessionId)) {
        setStatus("Could not delete the selected local conversation.");
        return;
    }
    const QVariantList remaining = m_store->sessions(1);
    const QString next = remaining.isEmpty() ? m_store->createSession()
        : remaining.first().toMap().value("id").toString();
    if (!next.isEmpty()) openSession(next);
    else clearConversation();
    setStatus("Deleted one selected conversation from local SQLite history.");
}
