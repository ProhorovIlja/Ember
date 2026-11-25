#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QStringList>
#include <array>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (app.arguments().size() != 3) return 2;
    const QString source = app.arguments().at(1);
    const QString target = app.arguments().at(2);
    QSvgRenderer svg(source);
    if (!svg.isValid()) return 3;
    constexpr std::array<int, 4> sizes = {16, 32, 48, 256};
    std::array<QByteArray, 4> images;
    for (qsizetype i = 0; i < qsizetype(sizes.size()); ++i) {
        const int size = sizes.at(i);
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            svg.render(&painter, QRectF(0, 0, size, size));
        }
        QBuffer buffer(&images.at(i));
        if (!buffer.open(QIODevice::WriteOnly) ||
            !image.save(&buffer, "PNG")) return 4;
    }

    QFile output(target);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 5;
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + quint32(sizes.size()) * 16;
    for (qsizetype i = 0; i < qsizetype(sizes.size()); ++i) {
        stream << quint8(sizes[i] == 256 ? 0 : sizes[i])
               << quint8(sizes[i] == 256 ? 0 : sizes[i])
               << quint8(0) << quint8(0)
               << quint16(1) << quint16(32)
               << quint32(images[i].size()) << offset;
        offset += quint32(images[i].size());
    }
    for (const auto &image : images) {
        if (stream.writeRawData(image.constData(), image.size()) != image.size())
            return 6;
    }
    output.close();
    return output.size() > 128 ? 0 : 7;
}
