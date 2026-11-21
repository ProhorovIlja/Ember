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
QJsonObject chatRequest(const QString &persona, const QList<Turn> &history, const QString &user, int maxTokens = 384, double temperature = 0.8);
bool allowedLocalEndpoint(const QString &url);
bool allowedLocalSpeechEndpoint(const QString &url);
QJsonObject speechRequest(const QString &model, const QString &voice, const QString &text);
bool validSpeechWav(const QByteArray &wav);
QStringList speechChunks(const QString &text, int maxCharacters = 620);
}
