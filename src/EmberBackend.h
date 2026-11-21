#pragma once
#include "Core.h"
#include "SessionStore.h"
#include <QFile>
#include <QLockFile>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioSource>
#include <QAudioSink>
#include <QIODevice>
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
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QString currentSessionId READ currentSessionId NOTIFY sessionsChanged)
    Q_PROPERTY(bool privateMode READ privateMode WRITE setPrivateMode NOTIFY privateModeChanged)
    Q_PROPERTY(QString continuityNotes READ continuityNotes NOTIFY continuityNotesChanged)
    Q_PROPERTY(QString recognitionHints READ recognitionHints NOTIFY recognitionHintsChanged)
    Q_PROPERTY(bool localHistoryReady READ localHistoryReady CONSTANT)
    Q_PROPERTY(int microphoneLevel READ microphoneLevel NOTIFY microphoneLevelChanged)
    Q_PROPERTY(bool autoEndOnSilence READ autoEndOnSilence WRITE setAutoEndOnSilence NOTIFY configurationChanged)
    Q_PROPERTY(bool dictationActive READ dictationActive NOTIFY dictationChanged)
    Q_PROPERTY(QString dictatedText READ dictatedText NOTIFY dictationChanged)
    Q_PROPERTY(bool brainReady READ brainReady NOTIFY brainReadyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)
    Q_PROPERTY(bool speechServerReady READ speechServerReady NOTIFY speechServerChanged)
    Q_PROPERTY(bool speechServerRunning READ speechServerRunning NOTIFY speechServerChanged)
    Q_PROPERTY(QString persona READ persona WRITE setPersona NOTIFY configurationChanged)
    Q_PROPERTY(int chatMode READ chatMode WRITE setChatMode NOTIFY configurationChanged)
    Q_PROPERTY(int recognitionLanguage READ recognitionLanguage WRITE setRecognitionLanguage NOTIFY configurationChanged)
    Q_PROPERTY(int recognitionEngine READ recognitionEngine WRITE setRecognitionEngine NOTIFY configurationChanged)
    Q_PROPERTY(int voiceId READ voiceId WRITE setVoiceId NOTIFY configurationChanged)
    Q_PROPERTY(int voiceEngine READ voiceEngine WRITE setVoiceEngine NOTIFY configurationChanged)
    Q_PROPERTY(int deliveryStyle READ deliveryStyle WRITE setDeliveryStyle NOTIFY configurationChanged)
    Q_PROPERTY(int outputVolume READ outputVolume WRITE setOutputVolume NOTIFY configurationChanged)
    Q_PROPERTY(bool earlySpeechEnabled READ earlySpeechEnabled WRITE setEarlySpeechEnabled NOTIFY configurationChanged)
    Q_PROPERTY(bool allowSpeechDownloads READ allowSpeechDownloads WRITE setAllowSpeechDownloads NOTIFY configurationChanged)
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
    QVariantList sessions() const { return m_sessions; }
    QString currentSessionId() const { return m_sessionId; }
    bool privateMode() const { return m_privateMode; }
    QString continuityNotes() const { return m_continuityNotes; }
    QString recognitionHints() const { return m_recognitionHints; }
    bool localHistoryReady() const { return m_store && m_store->ready(); }
    int microphoneLevel() const { return m_microphoneLevel; }
    bool autoEndOnSilence() const { return m_autoEndOnSilence; }
    bool dictationActive() const { return m_listening && m_captureDictation; }
    QString dictatedText() const { return m_dictatedText; }
    bool brainReady() const { return m_brainReady; }
    bool busy() const { return m_busy; }
    bool listening() const { return m_listening; }
    bool speaking() const { return m_speaking; }
    bool speechServerReady() const { return m_speechServerReady; }
    bool speechServerRunning() const { return m_speechServer.state() != QProcess::NotRunning; }
    QString persona() const { return m_persona; }
    int chatMode() const { return m_chatMode; }
    int recognitionLanguage() const { return m_recognitionLanguage; }
    int recognitionEngine() const { return m_recognitionEngine; }
    int voiceId() const { return m_voiceId; }
    int voiceEngine() const { return m_voiceEngine; }
    int deliveryStyle() const { return m_deliveryStyle; }
    int outputVolume() const { return m_outputVolume; }
    bool earlySpeechEnabled() const { return m_earlySpeechEnabled; }
    bool allowSpeechDownloads() const { return m_allowSpeechDownloads; }
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
    Q_INVOKABLE void setChatMode(int mode);
    Q_INVOKABLE void setRecognitionLanguage(int language);
    Q_INVOKABLE void setRecognitionEngine(int index);
    Q_INVOKABLE void setVoiceId(int index);
    Q_INVOKABLE void setVoiceEngine(int index);
    Q_INVOKABLE void setDeliveryStyle(int style);
    Q_INVOKABLE void setOutputVolume(int percent);
    Q_INVOKABLE void setEarlySpeechEnabled(bool value);
    Q_INVOKABLE void setAllowSpeechDownloads(bool value);
    Q_INVOKABLE void setLocalVoice(const QString &value);
    Q_INVOKABLE void setVoiceSample(const QString &value);
    Q_INVOKABLE void setLocalModel(const QString &value);
    Q_INVOKABLE void setInputDeviceIndex(int index);
    Q_INVOKABLE void setOutputDeviceIndex(int index);
    Q_INVOKABLE void startBrain();
    Q_INVOKABLE void stopBrain();
    Q_INVOKABLE void startSpeechServer();
    Q_INVOKABLE void stopSpeechServer();
    Q_INVOKABLE void submitText(const QString &text);
    Q_INVOKABLE QString formatReply(const QString &text) const;
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void startDictation();
    Q_INVOKABLE void setAutoEndOnSilence(bool enabled);
    Q_INVOKABLE void stopDictation();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void testVoice();
    Q_INVOKABLE void cancelWork();
    Q_INVOKABLE void clearConversation();
    Q_INVOKABLE void setPrivateMode(bool enabled);
    Q_INVOKABLE void openSession(const QString &id);
    Q_INVOKABLE void deleteCurrentSession();
    Q_INVOKABLE void setContinuityNotes(const QString &text);
    Q_INVOKABLE void setRecognitionHints(const QString &text);
    Q_INVOKABLE QVariantList searchHistory(const QString &text) const;
    Q_INVOKABLE void exportCurrentSession();
    Q_INVOKABLE void exportCompactHistory();
    Q_INVOKABLE void restoreCompactHistory();
signals:
    void statusChanged();
    void transcriptChanged();
    void lastReplyChanged();
    void voiceTimingsChanged();
    void messagesChanged();
    void sessionsChanged();
    void privateModeChanged();
    void continuityNotesChanged();
    void recognitionHintsChanged();
    void dictationChanged();
    void microphoneLevelChanged();
    void dictationReady(const QString &text);
    void brainReadyChanged();
    void busyChanged();
    void listeningChanged();
    void speakingChanged();
    void speechServerChanged();
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
    void transcribeViaNativeServer();
    void startCapture(bool dictation);
    void transcribeNextChunk();
    void completeTranscription();
    void failTranscription(const QString &reason);
    void refreshSessions();
    void rebuildConversationHistory();
    void renderChatStream();
    void finishChatStream(bool interrupted);
    void enqueueLiveSpeech(bool final);
    void beginSpeech(const QString &text);
    void playNextSpeechChunk();
    void synthesize(const QString &text);
    void synthesizeViaLocalServer(const QString &text);
    void synthesizeStreamingPcm(const QString &text);
    void readPcmResponse(QNetworkReply *reply);
    void pumpPcmPlayback();
    void resetPcmPlayback();
    void playSynthesizedFile();
    QString speechFile(int slot) const;
    void stopSpeechOnError(const QString &error);
    void pollBrain();
    void pollSpeechServer();
    void setSpeechServerReady(bool ready);
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
    QString m_localVoice = "vivian", m_localModel = "qwen3-tts-0.6b";
    QString m_voiceSample;
    QString m_sessionId, m_dictatedText, m_continuityNotes, m_recognitionHints;
    QString m_chatStreamText, m_chatStreamSession;
    QByteArray m_chatStreamBuffer;
    bool m_chatStreamDone = false, m_chatStreamInvalid = false;
    bool m_chatPreviewActive = false;
    QVariantList m_sessions;
    std::unique_ptr<SessionStore> m_store;
    QStringList m_voiceQueue;
    QVariantList m_messages;
    QList<EmberCore::Turn> m_history;
    int m_voiceId = 0, m_configurationRevision = 0, m_voiceEngine = 0, m_chatMode = 0;
    int m_recognitionLanguage = 0, m_recognitionEngine = 0;
    int m_deliveryStyle = 0, m_outputVolume = 85;
    int m_microphoneLevel = 0;
    bool m_autoEndOnSilence = false;
    bool m_brainReady = false, m_busy = false, m_listening = false;
    bool m_privateMode = false; // never stored in QSettings
    bool m_speaking = false, m_voicePending = false, m_cliTtsActive = false;
    bool m_cliAsrActive = false;
    bool m_speechSequence = false;
    bool m_chatGenerating = false;
    bool m_voiceEarlyActive = false, m_voiceEarlyFailed = false;
    bool m_earlySpeechEnabled = true;
    bool m_allowSpeechDownloads = false; // user explicit opt-in, not assumed
    bool m_speechTtsPathMode = false, m_speechAsrPathMode = false;
    qsizetype m_liveSpeechCursor = 0;
    bool m_speechServerReady = false;
    // One WAV plays while the opposite private slot is synthesized ahead.
    int m_playingSpeechSlot = -1, m_readySpeechSlot = -1, m_ttsOutputSlot = -1;
    QSettings m_settings;
    QMediaDevices m_mediaDevices;
    QByteArray m_inputId, m_outputId, m_captureDeviceId;
    std::unique_ptr<QTemporaryDir> m_work;
    std::unique_ptr<QLockFile> m_workLock;
    std::unique_ptr<QAudioSource> m_audioSource;
    QFile m_captureFile;
    std::unique_ptr<class CaptureTap> m_captureTap;
    QStringList m_transcriptionSegments;
    qint64 m_transcriptionOffset = 0, m_transcriptionSize = 0;
    qint64 m_transcriptionChunkSize = 0;
    bool m_captureDictation = false, m_transcribingDictation = false;
    QAudioFormat m_captureFormat;
    QAudioOutput m_audioOutput;
    QMediaPlayer m_mediaPlayer;
    std::unique_ptr<QAudioSink> m_pcmSink;
    QIODevice *m_pcmDevice = nullptr;
    QByteArray m_pcmPending;
    qint64 m_pcmTotalReceived = 0, m_pcmBytesSubmitted = 0;
    int m_pcmSampleRate = 0;
    bool m_pcmHttpDone = false, m_pcmStarted = false;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_speechReply;
    QPointer<QNetworkReply> m_nativeAsrReply;
    QProcess m_brain, m_whisper, m_tts, m_speechServer;
    QTimer m_healthTimer, m_speechHealthTimer;
    QTimer m_captureWatchdog;
    QTimer m_chatRenderTimer;
    QTimer m_pcmPumpTimer;
    quint64 m_captureSession = 0;
    QElapsedTimer m_brainElapsed, m_speechServerElapsed;
    quint64 m_brainSession = 0, m_speechServerSession = 0;
    QElapsedTimer m_asrElapsed, m_llmElapsed, m_ttsElapsed, m_voiceTurnElapsed, m_pcmDrainElapsed;
    qint64 m_asrLastMs = -1, m_llmLastMs = -1, m_ttsLastMs = -1;
    qint64 m_llmFirstTokenMs = -1, m_voiceFirstPlaybackMs = -1;
    bool m_voiceTurnActive = false;
};
