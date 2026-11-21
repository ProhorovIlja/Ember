#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
namespace EmberCore {
struct Turn { QString user; QString assistant; };
QByteArray pcmToWav(const QByteArray &pcm, int sampleRate);
QJsonObject chatRequest(const QString &persona, const QList<Turn> &history, const QString &user, int maxTokens = 384, double temperature = 0.8);
bool allowedLocalEndpoint(const QString &url);
}
