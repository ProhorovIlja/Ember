#pragma once
#include <QAudioFormat>
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
namespace EmberCore {
struct Turn { QString user; QString assistant; };
QByteArray pcmToWav(const QByteArray &pcm, int sampleRate);
// Offline 16 kHz mono conversion for common Windows capture devices.
QByteArray whisperWav(const QByteArray &raw, const QAudioFormat &source);
QJsonObject chatRequest(const QString &persona, const QList<Turn> &history, const QString &user, int maxTokens = 1024, double temperature = 0.8);
bool allowedLocalEndpoint(const QString &url);
bool allowedLocalSpeechEndpoint(const QString &url);
QJsonObject speechRequest(const QString &model, const QString &voice, const QString &text);
QJsonObject streamingPcmSpeechRequest(const QString &model, const QString &voice, const QString &text);
bool validPcmSampleRate(int rate);
bool validSpeechWav(const QByteArray &wav);
QStringList speechChunks(const QString &text, int maxCharacters = 620);
QString speechFriendlyText(const QString &markdown);
// Commit only stable complete sentences from an LLM stream. Reuses a
// caller-owned source cursor to avoid repeated speech or speaking partial code.
QStringList takeCommittedSpeech(const QString &draft, qsizetype &cursor, bool final);
bool mostlyCyrillic(const QString &text);
// Merge overlapped speech-recognition chunks without duplicating shared words.
QString joinTranscriptionSegments(const QStringList &segments);
// Safely assemble fragmented OpenAI/llama.cpp server-sent events; the buffer
// retains partial UTF-8 frames while the caller decides how to render text.
struct StreamResult { QString text; bool done = false; bool error = false; };
StreamResult consumeChatStream(QByteArray &buffer, const QByteArray &incoming);
}
