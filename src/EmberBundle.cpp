#include "EmberBundle.h"
#include "ImageLibrary.h"
#include "SessionStore.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>

namespace {
const QByteArray kMagic = QByteArrayLiteral("EMBER-COMPLETE-BACKUP-1\n");
constexpr quint64 kMaxChat = 256ULL * 1024 * 1024;
constexpr quint64 kMaxMedia = 1024ULL * 1024 * 1024;
constexpr quint32 kMaxPhotos = 4096;
constexpr quint32 kMaxImageBytes = 5 * 1024 * 1024;
constexpr qsizetype kChunk = 256 * 1024;

bool problem(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
bool copyExactly(QFile &input, QIODevice &output, quint64 bytes) {
    while (bytes > 0) {
        const qint64 next = qint64(std::min<quint64>(bytes, kChunk));
        const QByteArray data = input.read(next);
        if (data.size() != next || output.write(data) != next) return false;
        bytes -= quint64(next);
    }
    return true;
}
bool hashMatches(const QByteArray &bytes, const QString &id) {
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) == id;
}
QString mediaExtension(const QString &url) {
    return QUrl(url).toLocalFile().endsWith(".png", Qt::CaseInsensitive)
        ? ".png" : ".jpg";
}
}

bool EmberBundle::exportComplete(SessionStore &store, ImageLibrary &images,
                                 const QString &path, QString *error) {
    if (!store.ready() || path.trimmed().isEmpty())
        return problem(error, "Local SQLite history or output path unavailable.");
    QTemporaryDir staging;
    if (!staging.isValid())
        return problem(error, "Cannot prepare a private temporary archive.");
    const QString chatPath = staging.filePath("chats.emberbackup");
    if (!store.exportCompactBackup(chatPath))
        return problem(error, "Could not export full SQLite chat history.");
    QFile chat(chatPath);
    if (!chat.open(QIODevice::ReadOnly) ||
        chat.size() <= 0 || quint64(chat.size()) > kMaxChat)
        return problem(error, "Chat archive exceeds the 256 MiB per-backup limit.");
    if (images.imageCount() > int(kMaxPhotos))
        return problem(error, "Image Library has more than 4096 image assets.");
    const QVariantList assets = images.list(int(kMaxPhotos) + 1);
    if (assets.size() > int(kMaxPhotos) ||
        assets.size() != images.imageCount())
        return problem(error, "Image inventory is incomplete or contains invalid files.");
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return problem(error, "Could not open complete archive destination.");
    if (out.write(kMagic) != kMagic.size()) {
        out.cancelWriting();
        return problem(error, "Could not write complete archive header.");
    }
    QDataStream stream(&out);
    stream.setVersion(QDataStream::Qt_6_5);
    stream << quint64(chat.size());
    bool ok = stream.status() == QDataStream::Ok &&
        copyExactly(chat, out, quint64(chat.size()));
    if (ok) {
        stream << quint32(assets.size());
        ok = stream.status() == QDataStream::Ok;
    }
    quint64 totalMedia = 0;
    for (const QVariant &asset : assets) {
        if (!ok) break;
        const QVariantMap item = asset.toMap();
        const QString id = item.value("id").toString();
        const QString ext = mediaExtension(item.value("url").toString());
        const QByteArray bytes = images.verifiedBytes(id);
        if (bytes.isEmpty() || bytes.size() > kMaxImageBytes ||
            !hashMatches(bytes, id) ||
            (totalMedia += quint64(bytes.size())) > kMaxMedia) {
            ok = false;
            break;
        }
        stream << id << quint8(ext == ".png" ? 1 : 0)
               << quint32(bytes.size());
        ok = stream.status() == QDataStream::Ok &&
            out.write(bytes) == bytes.size();
    }
    if (ok) {
        stream << quint32(0xEBB00B01);
        ok = stream.status() == QDataStream::Ok;
    }
    if (!ok) {
        out.cancelWriting();
        return problem(error, "Image file verification or archive write failed.");
    }
    if (!out.commit())
        return problem(error, "Could not commit the complete backup atomically.");
    return true;
}

bool EmberBundle::restoreComplete(SessionStore &store, ImageLibrary &images,
                                  const QString &path, QString *error) {
    if (!store.ready())
        return problem(error, "Local SQLite history is not available.");
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly) ||
        in.read(kMagic.size()) != kMagic)
        return problem(error, "Unsupported or unreadable complete backup.");
    QTemporaryDir staging;
    if (!staging.isValid())
        return problem(error, "Could not create private recovery staging.");
    QDataStream stream(&in);
    stream.setVersion(QDataStream::Qt_6_5);
    quint64 chatSize = 0;
    stream >> chatSize;
    if (stream.status() != QDataStream::Ok ||
        chatSize == 0 || chatSize > kMaxChat)
        return problem(error, "Invalid embedded SQLite-chat archive size.");
    QFile chat(staging.filePath("chats.emberbackup"));
    if (!chat.open(QIODevice::WriteOnly) ||
        !copyExactly(in, chat, chatSize)) {
        return problem(error, "The embedded chat archive is truncated.");
    }
    chat.close();
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > kMaxPhotos)
        return problem(error, "Invalid complete backup image count.");
    struct Asset { QString id; bool png; QString staged; };
    QList<Asset> assets;
    QSet<QString> seen;
    const QRegularExpression hashRx(QStringLiteral("^[0-9a-f]{64}$"));
    quint64 totalMedia = 0;
    for (quint32 i = 0; i < count; ++i) {
        QString id;
        quint8 format = 0;
        quint32 bytes = 0;
        stream >> id >> format >> bytes;
        if (stream.status() != QDataStream::Ok ||
            !hashRx.match(id).hasMatch() || seen.contains(id) ||
            format > 1 || bytes == 0 || bytes > kMaxImageBytes ||
            (totalMedia += bytes) > kMaxMedia)
            return problem(error, "Invalid or duplicated image record.");
        const QByteArray raw = in.read(bytes);
        if (raw.size() != bytes || !hashMatches(raw, id))
            return problem(error, "Image SHA-256 mismatch or truncated archive.");
        QFile file(staging.filePath(id + (format == 1 ? ".png" : ".jpg")));
        if (!file.open(QIODevice::WriteOnly) || file.write(raw) != bytes)
            return problem(error, "Could not stage a verified image.");
        file.close();
        seen.insert(id);
        assets.append(Asset{id, format == 1, file.fileName()});
    }
    quint32 marker = 0;
    stream >> marker;
    if (stream.status() != QDataStream::Ok || marker != 0xEBB00B01 ||
        !in.atEnd())
        return problem(error, "Archive is incomplete or contains trailing data.");
    // No changes to SQLite until *all* image content has been verified.
    // Install images first, so a failed SQLite restore can remove new assets.
    QStringList newlyInstalled;
    for (const Asset &asset : assets) {
        QFile file(asset.staged);
        if (!file.open(QIODevice::ReadOnly))
            break;
        const QByteArray bytes = file.read(kMaxImageBytes + 1);
        bool created = false;
        if (bytes.isEmpty() || bytes.size() > kMaxImageBytes ||
            !images.installArchived(asset.id, bytes, asset.png, &created))
            break;
        if (created) newlyInstalled.append(asset.id);
    }
    if (newlyInstalled.size() > assets.size())
        return problem(error, "Unexpected asset restoration count.");
    bool allReady = true;
    for (const Asset &asset : assets)
        if (images.verifiedBytes(asset.id).isEmpty()) allReady = false;
    if (allReady && store.restoreCompactBackup(chat.fileName()))
        return true;
    for (const QString &id : newlyInstalled) images.remove(id);
    return problem(error, "Backup restore failed. Existing chats and images were retained.");
}
