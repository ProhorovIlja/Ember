#include "ImageLibrary.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

namespace {
constexpr qint64 kMaxSourceBytes = 15LL * 1024 * 1024;
constexpr qint64 kMaxStoredBytes = 5LL * 1024 * 1024;
constexpr qint64 kMaxSourcePixels = 25000000;
constexpr int kMaxEdge = 1536;
const QRegularExpression kIdPattern("^[0-9a-f]{64}$");
void setError(QString *error, const QString &message) {
    if (error) *error = message;
}
bool allowedImage(const QImage &image) {
    return !image.isNull() && image.width() > 0 && image.height() > 0 &&
        image.width() <= 10000 && image.height() <= 10000 &&
        qint64(image.width()) * image.height() <= kMaxSourcePixels;
}
}

ImageLibrary::ImageLibrary(const QString &root) : m_root(root) {
    QDir().mkpath(m_root);
}
QString ImageLibrary::importFile(const QString &path, QString *error) {
    const QFileInfo file(path);
    if (!file.isFile() || file.isSymLink() || !file.isReadable() ||
        file.size() <= 0 || file.size() > kMaxSourceBytes) {
        setError(error, "Image is unreadable, linked or exceeds 15 MiB.");
        return {};
    }
    QImageReader reader(file.absoluteFilePath());
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);
    const QSize declared = reader.size();
    if (declared.isValid() &&
        (declared.width() > 10000 || declared.height() > 10000 ||
         qint64(declared.width()) * declared.height() > kMaxSourcePixels)) {
        setError(error, "Image dimensions exceed the safe decode limit.");
        return {};
    }
    const QImage decoded = reader.read();
    if (!allowedImage(decoded)) {
        setError(error, "Unsupported or oversized image content.");
        return {};
    }
    return importImage(decoded, error);
}
QString ImageLibrary::importImage(const QImage &image, QString *error) {
    if (!allowedImage(image)) {
        setError(error, "Clipboard image is missing or exceeds the safe pixel limit.");
        return {};
    }
    // Normalize all input formats and strip EXIF, comments and source paths.
    const QImage scaled = image.scaled(kMaxEdge, kMaxEdge,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const bool transparent = scaled.hasAlphaChannel();
    QImage clean(scaled.size(), transparent ? QImage::Format_ARGB32
                                           : QImage::Format_RGB32);
    clean.fill(transparent ? Qt::transparent : Qt::white);
    {
        QPainter painter(&clean);
        painter.drawImage(0, 0, scaled);
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) ||
        !clean.save(&buffer, transparent ? "PNG" : "JPG", transparent ? -1 : 85) ||
        bytes.isEmpty() || bytes.size() > kMaxStoredBytes) {
        setError(error, "The normalized image exceeds 5 MiB or cannot be encoded.");
        return {};
    }
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (!QDir().mkpath(m_root)) {
        setError(error, "Local image directory is not writable.");
        return {};
    }
    const QString target = QDir(m_root).filePath(
        digest + (transparent ? ".png" : ".jpg"));
    if (QFileInfo::exists(target)) {
        if (filePath(digest).isEmpty()) {
            setError(error, "A conflicting stored image failed integrity checks.");
            return {};
        }
        return digest; // identical bytes consume no extra disk
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error, "Could not save the image atomically in the local Library.");
        return {};
    }
    return digest;
}
QString ImageLibrary::filePath(const QString &id) const {
    if (!kIdPattern.match(id).hasMatch()) return {};
    for (const char *extension : {".jpg", ".png"}) {
        const QFileInfo candidate(QDir(m_root).filePath(id + QLatin1String(extension)));
        if (!candidate.isFile() || candidate.isSymLink() ||
            candidate.size() <= 0 || candidate.size() > kMaxStoredBytes) continue;
        const QString folder = QDir::fromNativeSeparators(QDir(m_root).canonicalPath());
        const QString actual = QDir::fromNativeSeparators(candidate.canonicalFilePath());
        if (!actual.startsWith(folder + '/', Qt::CaseInsensitive)) continue;
        return actual;
    }
    return {};
}
QString ImageLibrary::fileUrl(const QString &id) const {
    const QString path = filePath(id);
    return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString();
}
int ImageLibrary::imageCount() const {
    return QDir(m_root).entryList({"*.jpg", "*.png"},
        QDir::Files | QDir::NoSymLinks).size();
}
QByteArray ImageLibrary::verifiedBytes(const QString &id) const {
    const QString path = filePath(id);
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.read(kMaxStoredBytes + 1);
    if (bytes.isEmpty() || bytes.size() > kMaxStoredBytes ||
        QString::fromLatin1(QCryptographicHash::hash(
            bytes, QCryptographicHash::Sha256).toHex()) != id) return {};
    return bytes;
}
bool ImageLibrary::installArchived(const QString &id,
                                   const QByteArray &encoded,
                                   bool png, bool *created) {
    if (created) *created = false;
    if (!kIdPattern.match(id).hasMatch() ||
        encoded.isEmpty() || encoded.size() > kMaxStoredBytes ||
        QString::fromLatin1(QCryptographicHash::hash(
            encoded, QCryptographicHash::Sha256).toHex()) != id ||
        !(png ? encoded.startsWith("\x89PNG\r\n\x1a\n")
               : encoded.startsWith("\xff\xd8"))) return false;
    QBuffer buffer;
    buffer.setData(encoded);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QImageReader reader(&buffer, png ? "PNG" : "JPG");
    const QSize dimensions = reader.size();
    if (!dimensions.isValid() || dimensions.width() > 1536 ||
        dimensions.height() > 1536 ||
        qint64(dimensions.width()) * dimensions.height() > 2500000 ||
        !reader.canRead()) return false;
    // An identical digest already installed under the other extension should
    // be reused, not stored as a second copy.
    if (!filePath(id).isEmpty()) return verifiedBytes(id) == encoded;
    const QString target = QDir(m_root).filePath(
        id + (png ? ".png" : ".jpg"));
    if (QFileInfo::exists(target)) return false;
    if (!QDir().mkpath(m_root)) return false;
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(encoded) != encoded.size() || !file.commit()) return false;
    if (created) *created = true;
    return true;
}
QByteArray ImageLibrary::dataUri(const QString &id) const {
    const QString path = filePath(id);
    if (path.isEmpty()) return {};
    const QByteArray data = verifiedBytes(id);
    if (data.isEmpty()) return {};
    return QByteArray(path.endsWith(".png", Qt::CaseInsensitive)
                      ? "data:image/png;base64," : "data:image/jpeg;base64,")
         + data.toBase64();
}
QVariantList ImageLibrary::list(int limit) const {
    QVariantList result;
    const QDir root(m_root);
    const auto entries = root.entryInfoList({"*.jpg", "*.png"},
        QDir::Files | QDir::NoSymLinks | QDir::Readable, QDir::Time);
    const int maximum = std::clamp(limit, 1, 4097);
    for (const QFileInfo &entry : entries) {
        const QString id = entry.completeBaseName();
        const QString url = fileUrl(id);
        if (url.isEmpty()) continue;
        result.append(QVariantMap{
            {"id", id}, {"url", url}, {"bytes", entry.size()},
            {"label", id.left(12).toUpper()}
        });
        if (result.size() >= maximum) break;
    }
    return result;
}
bool ImageLibrary::remove(const QString &id) {
    const QString path = filePath(id);
    return !path.isEmpty() && QFile::remove(path);
}
