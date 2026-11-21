#pragma once
#include <QImage>
#include <QString>
#include <QVariantList>

// Native local-only gallery: images are normalized to bounded PNG/JPEG files.
// No raw camera metadata, clipboard contents or network requests are kept.
class ImageLibrary final {
public:
    explicit ImageLibrary(const QString &root);
    QString importFile(const QString &path, QString *error = nullptr);
    QString importImage(const QImage &image, QString *error = nullptr);
    QString filePath(const QString &id) const;
    QString fileUrl(const QString &id) const;
    QByteArray dataUri(const QString &id) const;
    QVariantList list(int limit = 120) const;
    bool remove(const QString &id);
private:
    QString m_root;
};
