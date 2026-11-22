#include "WikipediaSearch.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

namespace EmberWikipedia {
namespace {
constexpr qsizetype kMaxResponse = 128 * 1024;
constexpr qsizetype kMaxPreview = 3900;

bool allowedQuery(const QString &query) {
    if (query.trimmed().size() < 2 || query.size() > 120 ||
        query.contains('\n') || query.contains('\r')) return false;
    for (const QChar c : query) {
        if (c.isNull() || c.unicode() < 0x20 || c.unicode() == 0x7F)
            return false;
    }
    return true;
}
QString cleanText(QString value, int limit) {
    // MediaWiki search excerpts contain <span class="searchmatch"> markup.
    // Keep them as plain data; never render arbitrary remote HTML.
    static const QRegularExpression tag(QStringLiteral("<[^>]{0,512}>"));
    value.replace(tag, QStringLiteral(" "));
    value.replace("&lt;", "<");
    value.replace("&gt;", ">");
    value.replace("&quot;", "\"");
    value.replace("&#39;", "'");
    value.replace("&nbsp;", " ");
    value.replace("&amp;", "&"); // decode last to avoid nested entity behavior
    for (QChar &c : value) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F ||
            c.unicode() == 0x2028 || c.unicode() == 0x2029)
            c = QLatin1Char(' ');
    }
    return value.simplified().left(limit);
}
}
QUrl searchUrl(const QString &query, const QString &language) {
    if ((language != "en" && language != "ru") || !allowedQuery(query))
        return {};
    QUrl url(QString("https://%1.wikipedia.org/w/rest.php/v1/search/page")
                 .arg(language));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("q"), query.trimmed());
    params.addQueryItem(QStringLiteral("limit"), QStringLiteral("5"));
    url.setQuery(params);
    if (!url.isValid() || url.scheme() != "https" ||
        url.host() != language + ".wikipedia.org" || url.port(-1) != -1)
        return {};
    return url;
}
Preview parseSearch(const QString &query, const QString &language,
                    const QByteArray &response) {
    if (searchUrl(query, language).isEmpty())
        return {{}, "Invalid search query or Wikipedia language.", 0};
    if (response.isEmpty() || response.size() > kMaxResponse)
        return {{}, "Wikipedia JSON response was empty or oversized.", 0};
    QJsonParseError error{};
    const QJsonDocument parsed = QJsonDocument::fromJson(response, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject() ||
        !parsed.object().value("pages").isArray())
        return {{}, "Wikipedia did not return a valid search result list.", 0};
    const QJsonArray pages = parsed.object().value("pages").toArray();
    Preview result;
    result.text = QStringLiteral(
        "\n\n---\nWikipedia %1 search for \"%2\": untrusted reference snippets, "
        "NOT full verified articles. Inspect the cited URLs before trusting claims.\n")
                      .arg(language, cleanText(query, 120));
    for (const QJsonValue &value : pages) {
        if (!value.isObject()) continue;
        const QJsonObject entry = value.toObject();
        const QString key = entry.value("key").toString();
        const QString title = cleanText(entry.value("title").toString(), 150);
        if (key.isEmpty() || key.size() > 250 || title.isEmpty() ||
            key.contains('\n') || key.contains('\r')) continue;
        const QString description =
            cleanText(entry.value("description").toString(), 220);
        const QString excerpt = cleanText(entry.value("excerpt").toString(), 400);
        const QString url = "https://" + language +
            ".wikipedia.org/wiki/" +
            QString::fromLatin1(QUrl::toPercentEncoding(key, "_-"));
        const QString block = QString("\n%1. %2\n%3\nExcerpt: %4\nSource: %5\n")
            .arg(result.count + 1).arg(title).arg(description, excerpt, url);
        if (result.text.size() + block.size() + 60 > kMaxPreview) break;
        result.text += block;
        if (++result.count == 5) break;
    }
    if (result.count == 0) result.text += "\nNo matching article snippets in this response.\n";
    result.text += "\n--- End Wikipedia search excerpts ---";
    return result;
}
}
