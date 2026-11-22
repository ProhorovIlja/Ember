#pragma once
#include <QAudioFormat>
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
class QFile;
namespace EmberCore {
// Maximum streamed model text and the slightly larger on-screen/SQLite
// message including a verified token-budget or interruption annotation.
inline constexpr int kMaxVisibleChatCharacters = 65536;
inline constexpr int kMaxArchivedChatCharacters = kMaxVisibleChatCharacters + 128;
struct Turn { QString user; QString assistant; };
// Non-persisted capture-energy inspection, not speech classification.
struct AudioLevel { double rms = 0.0; double peak = 0.0; int durationMs = 0; };
AudioLevel pcmLevel(const char *bytes, qsizetype length, const QAudioFormat &format);
// An OPTIONAL experimental pause endpointer: speech must first be sustained,
// then quiet must persist. Manual stop remains available at all times.
class VoiceEndpointer {
public:
    void reset();
    bool observe(const AudioLevel &level);
private:
    int m_elapsedMs = 0, m_candidateMs = 0, m_silentMs = 0;
    bool m_heardSpeech = false;
};
QByteArray pcmToWav(const QByteArray &pcm, int sampleRate);
// Conservative energy-only pause alignment within the final 1.2 seconds of
// an ASR chunk. Returns freshBytes unchanged if no unambiguous quiet interval.
// The candidate contains prefixBytes of previous audio plus freshBytes of new
// audio; no audio is dropped or uploaded by this boundary computation.
qint64 speechAwareAsrFreshBytes(const QByteArray &candidate,
                                const QAudioFormat &format,
                                qint64 prefixBytes, qint64 freshBytes);
// Offline 16 kHz mono conversion for common Windows capture devices.
QByteArray whisperWav(const QByteArray &raw, const QAudioFormat &source);
QJsonObject chatRequest(const QString &persona, const QList<Turn> &history, const QString &user, int maxTokens = 1024, double temperature = 0.8,
                        const QString &historicalExcerpts = {},
                        bool enableThinking = false,
                        int maxOutputCap = 0,
                        int finiteContextTokens = 0);
// Bounded per-request output tokens; keeps half the active finite context
// available for the prompt/history, even with a larger user-selected budget.
int chatOutputBudget(int requestedTokens, int contextTokens, bool voice);
// Bound streamed visible characters in proportion to the ACTUAL model output
// token cap, not an unrelated hard-coded 16K QChar limit. This remains a
// defensive finite cap, not an assertion about any model's tokenization.
int chatVisibleCharacterLimit(int outputTokens, bool voice);
// Text-first: long and precise technical replies, creative fiction separately.
struct ChatGenerationProfile { int maxTokens; double temperature; };
ChatGenerationProfile chatGenerationProfile(int mode, bool voice, bool deep = false);
// Windows: prove the IPv4 127.0.0.1 listener belongs to the exact managed
// QProcess PID. HTTP 200 on /health alone is not authentication. Returns
// false when the listener is absent, wildcard-bound or cannot be inspected.
bool ownedLoopbackTcpListener(quint16 port, quint64 processId);
// Require the actual bounded JSON health contract, not HTTP 200 alone.
// Accept application/json with an optional charset for llama.cpp/speech.cpp.
bool validManagedHealthResponse(const QByteArray &body, const QByteArray &contentType);
bool allowedLocalEndpoint(const QString &url);
// Dictation may send only after the user selects Send, ASR finishes,
// recording was intact, and the local model is available.
bool maySendCompletedDictation(bool userSelectedSend, bool captureInterrupted,
                              bool brainReady);
// The foreground QML mic button supports both tap-to-record and hold/release
// to transcribe a draft. Only a genuine active capture may be finished.
bool holdDictationReleasedToDraft(qint64 heldMilliseconds, bool captureActive);
// Full Voice mode: a genuine active voice capture may finish on held release.
bool holdVoiceReleasedToReply(qint64 heldMilliseconds, bool captureActive);
bool allowedLocalSpeechEndpoint(const QString &url);
bool allowedLocalAsrEndpoint(const QString &url);
QString nativeAsrModel(int recognitionEngine);
QString recognitionLanguageTag(int languageChoice);
QString compactAsrHints(const QString &hints);
// No shell is involved: returns an explicit QProcess argv array. Applies
// optional user-entered, conversation-local terminology to Whisper CLI.
QStringList whisperCliArguments(const QString &model, const QString &wav,
                                const QString &outputPrefix, int languageChoice,
                                const QString &hints,
                                const QString &optionalVadModel);
// Whisper CLI files and native ASR HTTP bodies must be well-formed UTF-8.
// An empty, valid transcript means silence; malformed bytes must fail closed.
QString decodeLocalAsrUtf8(const QByteArray &raw, bool *valid = nullptr);
QJsonObject speechRequest(const QString &model, const QString &voice, const QString &text);
QJsonObject streamingPcmSpeechRequest(const QString &model, const QString &voice,
                                      const QString &text, int deliveryStyle = 0);
QString nativeDeliveryInstructions(int style);
bool pcmPlaybackDrained(qint64 submittedBytes, int sampleRate, qint64 processedUsecs,
                        int channels = 1);

// Converts mono little-endian PCM16 streaming chunks into a supported Windows
// sink format without a WAV file or a growing intermediate buffer. One source
// sample of lookahead keeps resampling continuous at arbitrary HTTP boundaries.
// Linear interpolation is a compatibility fallback; native-rate delivery is
// preferred when the output device supports it.
class Pcm16OutputConverter {
public:
    bool configure(int sourceRate, int outputRate, int channels,
                   QAudioFormat::SampleFormat sampleFormat);
    bool feed(const QByteArray &input, QByteArray &output);
    bool finish(QByteArray &output);
    void clear();
    int bytesPerFrame() const { return m_channels * m_sampleBytes; }
    int outputRate() const { return m_outputRate; }
    int sourceRate() const { return m_sourceRate; }
private:
    void appendFrame(QByteArray &output, qint16 value) const;
    int m_sourceRate = 0, m_outputRate = 0;
    int m_channels = 0, m_sampleBytes = 0;
    QAudioFormat::SampleFormat m_sampleFormat = QAudioFormat::Unknown;
    qint64 m_sourceFrames = 0, m_outputFrames = 0;
    qint16 m_previous = 0;
    int m_carry = -1;
    bool m_ready = false, m_closed = false;
};
// Finite local inference profiles. User can shrink the GPU allocation or
// increase context only after stopping the owned model process.
bool validBrainLaunchProfile(int contextTokens, int gpuLayers);
QStringList llamaLocalLaunchArguments(const QString &gguf, int contextTokens,
                                     int gpuLayers);
bool validPcmSampleRate(int rate);
bool safeScratchSessionName(const QString &name);
// Close the microphone handle first, then delete only Ember-owned scratch files.
// A Windows backend may temporarily lock one of the files; false requests a
// bounded retry after its writer/network upload has exited.
bool discardPrivateVoiceScratch(QFile &microphone, const QString &folder);
bool shouldPersistConversation(bool privateMode, const QString &sessionId);
// A completed HTTP 200 stream stopped at its model token budget is incomplete
// text, not a failed transport. Only then may already generated speech finish.
bool canSpeakStreamedReply(bool voiceRequested, bool interrupted,
                           bool verifiedTokenLimit);
// User-facing status markers belong to the persisted/display text only;
// the live TTS stream must consume the unmodified, visible model response.
QString archivedStreamedReply(const QString &visibleText, bool interrupted,
                              bool verifiedTokenLimit);
// Half-duplex user-controlled voice interruption; never interrupt active ASR.
bool captureAllowedWithInterruption(bool busy, bool listening,
                                    bool speechActive, bool recognitionActive);
// Stopping the optional text LLM must not erase independent RU/EN dictation.
// Only a genuine dictation capture or transcription is preserved; spoken
// question/reply turns still require the text brain and must be stopped.
bool brainStopKeepsDictation(bool listening, bool captureIsDictation,
                            bool transcribingDictation);
// A recording must never exceed its scratch quota or end on a partial PCM frame.
qint64 allowedCaptureBytes(qint64 alreadyWritten, qint64 requested,
                           int bytesPerFrame, qint64 quota = 1024LL * 1024 * 1024);
// A microphone hotplug/interruption can be salvaged as an EDITABLE dictation
// draft only after close+integrity checks. Never salvage a voice command that
// would be sent automatically or files with a partial PCM frame.
bool canRecoverInterruptedDictation(bool dictation, bool writeFailed,
    qint64 observedWrittenBytes, qint64 fileSize, const QAudioFormat &format);
// Render a restricted local-only subset of Markdown. No external-resource HTML.
QString safeMarkdownHtml(const QString &markdown);
// Image references are local SHA-256 identifiers in text archive only.
// Never accept network URLs or raw model-supplied HTML for image rendering.
QString attachedImageId(const QString &text);
QString stripAttachedImageMarker(const QString &text);
// Builds one typed-content multimodal part for the current user request.
bool attachLocalVisionData(QJsonObject &request, const QByteArray &dataUri);
// User-supplied local GGUF model path, never a catalog download alias.
QString offlineSpeechModelFile(const QString &applicationRoot, const QString &alias);
// Explicit user-picked file on any local drive. Only a structural GGUF
// preflight; compatible model family, provenance and license need real QA.
QString localSpeechGgufFile(const QString &absoluteFilePath);
// Upstream speech.cpp performs a once-a-day Internet version check by
// default; disable it for EVERY managed launch, including offline weights.
QProcessEnvironment speechProcessEnvironment(QProcessEnvironment inherited);
// Speech.cpp device choices are explicit; CPU can free RTX 4070 VRAM for LLM.
QString nativeSpeechDeviceArg(int deviceIndex);
bool validSpeechWav(const QByteArray &wav);
// Suppress hallucinations only for *digitally near-silent* canonical 16kHz
// mono PCM16 input; never gate ordinary quiet voices or unrecognized WAVs.
bool nearSilentAsrWav(const QByteArray &wav);
QStringList speechChunks(const QString &text, int maxCharacters = 620);
// A constant-memory streaming filter. Never retain hidden reasoning tokens,
// and withhold only partial <think> or </think> marker fragments (<= 7 chars).
struct VisibleAssistantStream {
    QString possibleTag;
    bool insideThought = false;
};
QString consumeVisibleAssistantDelta(VisibleAssistantStream &state, const QString &delta);
// Compatibility projection for bounded, already-accumulated responses/tests.
// Live SSE should instead use consumeVisibleAssistantDelta incrementally.
QString visibleAssistantText(const QString &draft);
QString speechFriendlyText(const QString &markdown);
// Commit only stable complete sentences from an LLM stream. Reuses a
// caller-owned source cursor to avoid repeated speech or speaking partial code.
QStringList takeCommittedSpeech(const QString &draft, qsizetype &cursor, bool final);
bool mostlyCyrillic(const QString &text);
// Merge overlapped speech-recognition chunks without duplicating shared words.
QString joinTranscriptionSegments(const QStringList &segments);
// Preserve already recognized RU/EN dictation on a late model failure.
// Never salvage voice requests that would be submitted automatically.
QString partialDictationOnError(bool dictation, const QStringList &segments);
// Safely assemble fragmented OpenAI/llama.cpp server-sent events; the buffer
// retains partial UTF-8 frames while the caller decides how to render text.
struct StreamResult {
    QString text;
    bool done = false, error = false, truncated = false;
};
StreamResult consumeChatStream(QByteArray &buffer, const QByteArray &incoming);
// O(total received bytes) SSE framing for tiny TCP fragments.
struct SseEventState {
    QByteArray buffer;
    qsizetype scanAt = 0;
    qsizetype lineStart = 0;
    void reset() { buffer.clear(); scanAt = lineStart = 0; }
};
StreamResult consumeChatStream(SseEventState &state, const QByteArray &incoming);
}
