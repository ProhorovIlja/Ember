#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
namespace EmberWikipedia {
struct Preview {
    QString text;
    QString error;
    int count = 0;
    bool ok() const { return error.isEmpty(); }
};
// Construct exactly one Wikimedia REST API search URL. No arbitrary endpoints,
// redirects, hidden searches, keys or automatic downloads.
QUrl searchUrl(const QString &query, const QString &language);
Preview parseSearch(const QString &query, const QString &language,
                    const QByteArray &response);
}
