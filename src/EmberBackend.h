#pragma once
#include "Core.h"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioSource>
#include <QBuffer>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>
#include <memory>
class EmberBackend final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(QString lastReply READ lastReply NOTIFY lastReplyChanged)
    Q_PROPERTY(QString voiceTimings READ voiceTimings NOTIFY voiceTimingsChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool brainReady READ brainReady NOTIFY brainReadyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)
    Q_PROPERTY(QString persona READ persona WRITE setPersona NOTIFY configurationChanged)
    Q_PROPERTY(int voiceId READ voiceId WRITE setVoiceId NOTIFY configurationChanged)
    Q_PROPERTY(int voiceEngine READ voiceEngine WRITE setVoiceEngine NOTIFY configurationChanged)
    Q_PROPERTY(QString localVoice READ localVoice WRITE setLocalVoice NOTIFY configurationChanged)
    Q_PROPERTY(QString voiceSample READ voiceSample WRITE setVoiceSample NOTIFY configurationChanged)
    Q_PROPERTY(QString localModel READ localModel WRITE setLocalModel NOTIFY configurationChanged)
    Q_PROPERTY(int configurationRevision READ configurationRevision NOTIFY configurationChanged)
    Q_PROPERTY(QVariantList audioInputs READ audioInputs NOTIFY audioDevicesChanged)
    Q_PROPERTY(QVariantList audioOutputs READ audioOutputs NOTIFY audioDevicesChanged)
    Q_PROPERTY(int selectedInputIndex READ selectedInputIndex NOTIFY audioDevicesChanged)
    Q_PROPERTY(int selectedOutputIndex READ selectedOutputIndex NOTIFY audioDevicesChanged)
public:
    explicit EmberBackend(QObject *parent = nullptr);
    ~EmberBackend() override;
    QString status() const { return m_status; }
    QString transcript() const { return m_transcript; }
    QString lastReply() const { return m_lastReply; }
    QString voiceTimings() const;
    QVariantList messages() const { return m_messages; }
    bool brainReady() const { return m_brainReady; }
    bool busy() const { return m_busy; }
    bool listening() const { return m_listening; }
    bool speaking() const { return m_speaking; }
    QString persona() const { return m_persona; }
    int voiceId() const { return m_voiceId; }
    int voiceEngine() const { return m_voiceEngine; }
    QString localVoice() const { return m_localVoice; }
    QString voiceSample() const { return m_voiceSample; }
    QString localModel() const { return m_localModel; }
    int configurationRevision() const { return m_configurationRevision; }
    QVariantList audioInputs() const;
    QVariantList audioOutputs() const;
    int selectedInputIndex() const;
    int selectedOutputIndex() const;
    Q_INVOKABLE QString pathFor(const QString &key) const;
    Q_INVOKABLE void choosePath(const QString &key);
    Q_INVOKABLE void setPersona(const QString &persona);
    Q_INVOKABLE void setVoiceId(int index);
    Q_INVOKABLE void setVoiceEngine(int index);
    Q_INVOKABLE void setLocalVoice(const QString &value);
    Q_INVOKABLE void setVoiceSample(const QString &value);
    Q_INVOKABLE void setLocalModel(const QString &value);
    Q_INVOKABLE void setInputDeviceIndex(int index);
    Q_INVOKABLE void setOutputDeviceIndex(int index);
    Q_INVOKABLE void startBrain();
    Q_INVOKABLE void stopBrain();
    Q_INVOKABLE void submitText(const QString &text);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void testVoice();
    Q_INVOKABLE void cancelWork();
    Q_INVOKABLE void clearConversation();
signals:
    void statusChanged();
    void transcriptChanged();
    void lastReplyChanged();
    void voiceTimingsChanged();
    void messagesChanged();
    void brainReadyChanged();
    void busyChanged();
    void listeningChanged();
    void speakingChanged();
    void configurationChanged();
    void audioDevicesChanged();
private:
    void setStatus(const QString &text);
    void setBusy(bool value);
    void setListening(bool value);
    void setSpeaking(bool value);
    void setBrainReady(bool value);
    void appendMessage(const QString &role, const QString &text);
    void submit(const QString &text, bool asVoice);
    void transcribe();
    void beginSpeech(const QString &text);
    void playNextSpeechChunk();
    void synthesize(const QString &text);
    void synthesizeViaLocalServer(const QString &text);
    void playSynthesizedFile();
    void pollBrain();
    void storeConfig();
    QString configPath(const QString &key) const;
    QString workFile(const QString &name) const;
    bool isRegularFile(const QString &path) const;
    void releaseCapture();
    QAudioDevice activeInput() const;
    void onInputInventoryChanged();
    void onOutputInventoryChanged();
    QString m_status = "Ready to configure local engines.";
    QString m_transcript, m_lastReply, m_pendingUser, m_persona;
    QString m_localVoice = "Vivian", m_localModel = "qwen3-tts";
    QString m_voiceSample;
    QStringList m_voiceQueue;
    QVariantList m_messages;
    QList<EmberCore::Turn> m_history;
    int m_voiceId = 0, m_configurationRevision = 0, m_voiceEngine = 0;
    bool m_brainReady = false, m_busy = false, m_listening = false;
    bool m_speaking = false, m_voicePending = false, m_cliTtsActive = false;
    bool m_speechSequence = false;
    QSettings m_settings;
    QMediaDevices m_mediaDevices;
    QByteArray m_inputId, m_outputId, m_captureDeviceId;
    std::unique_ptr<QTemporaryDir> m_work;
    std::unique_ptr<QAudioSource> m_audioSource;
    QBuffer m_audioBuffer;
    QByteArray m_pcm;
    QAudioFormat m_captureFormat;
    QAudioOutput m_audioOutput;
    QMediaPlayer m_mediaPlayer;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_speechReply;
    QProcess m_brain, m_whisper, m_tts;
    QTimer m_healthTimer;
    QTimer m_captureWatchdog;
    quint64 m_captureSession = 0;
    QElapsedTimer m_brainElapsed;
    quint64 m_brainSession = 0;
    QElapsedTimer m_asrElapsed, m_llmElapsed, m_ttsElapsed;
    qint64 m_asrLastMs = -1, m_llmLastMs = -1, m_ttsLastMs = -1;
};
