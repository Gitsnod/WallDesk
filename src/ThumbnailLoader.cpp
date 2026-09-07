#include "ThumbnailLoader.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QLinearGradient>
#include <QPainter>
#include <QPointer>
#include <QPolygonF>
#include <QSet>
#include "AppPaths.h"

#include <QStandardPaths>
#include <QThreadPool>
#include <QtGlobal>
#include <QtCore/QRunnable>
#include <QtCore/QMetaObject>

#include <atomic>

namespace {

/** 缓存条目上限，超出后清理最旧的一半，防止长期占用磁盘。 */
constexpr int kMaxCacheFiles = 2000;
constexpr int kTrimCheckEvery = 50;

std::atomic<int> gSaveCounter{0};

QString hashKey(const QString& path, const QSize& size)
{
    const QFileInfo info(path);
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(path.toUtf8());
    hash.addData(QByteArray::number(info.size()));
    hash.addData(QByteArray::number(info.lastModified().toSecsSinceEpoch()));
    hash.addData(QByteArray::number(size.width()));
    hash.addData(QByteArray::number(size.height()));
    return QString::fromLatin1(hash.result().toHex());
}

QString cacheFileFor(const QString& path, const QSize& size)
{
    return ThumbnailLoader::cacheDir() + QStringLiteral("/") + hashKey(path, size) + QStringLiteral(".png");
}

/** 居中裁剪：先按短边缩放到目标框，再取中间区域，得到填充式缩略图。 */
QImage cropCenter(const QImage& source, const QSize& size)
{
    const int x = (source.width() - size.width()) / 2;
    const int y = (source.height() - size.height()) / 2;
    return source.copy(qMax(0, x), qMax(0, y), qMin(size.width(), source.width()),
                       qMin(size.height(), source.height()));
}

QImage decodeImageFile(const QString& path, const QSize& size)
{
    QImageReader reader(path);
    reader.setAutoTransform(true); // 尊重 EXIF 方向
    const QSize original = reader.size();
    if (original.isValid() && !original.isEmpty()) {
        QSize scaled = original;
        scaled.scale(size, Qt::KeepAspectRatioByExpanding);
        reader.setScaledSize(scaled);
    }
    const QImage img = reader.read();
    if (img.isNull()) {
        return img;
    }
    return cropCenter(img, size);
}

/** 缓存条目过多时，按修改时间删除最旧的一半。 */
void trimCacheIfNeeded()
{
    if (gSaveCounter.fetch_add(1) % kTrimCheckEvery != 0) {
        return;
    }
    QDir dir(ThumbnailLoader::cacheDir());
    const QFileInfoList files = dir.entryInfoList(QStringList{QStringLiteral("*.png")},
                                                  QDir::Files, QDir::Time);
    if (files.size() <= kMaxCacheFiles) {
        return;
    }
    for (int i = kMaxCacheFiles / 2; i < files.size(); ++i) {
        QFile::remove(files.at(i).absoluteFilePath());
    }
}

/** 解码或抓帧，优先读缓存。在工作线程执行。 */
QImage loadOrGenerate(const QString& path, bool isVideo, const QSize& size,
                      const std::function<QImage(const QString&, const QSize&)>& grabber)
{
    const QString cache = cacheFileFor(path, size);
    QImage cachedImage;
    if (cachedImage.load(cache)) {
        return cachedImage;
    }
    if (!QFileInfo::exists(path)) {
        return QImage();
    }

    QImage result = isVideo ? (grabber ? grabber(path, size) : QImage())
                            : decodeImageFile(path, size);
    if (result.isNull()) {
        return QImage();
    }

    QDir().mkpath(ThumbnailLoader::cacheDir());
    QImageWriter writer(cache, "png");
    writer.setQuality(80);
    if (writer.write(result)) {
        trimCacheIfNeeded();
    }
    return result;
}

class ThumbTask : public QRunnable {
public:
    ThumbTask(QString path, bool isVideo, QSize size, QPointer<ThumbnailLoader> owner,
              std::function<QImage(const QString&, const QSize&)> grabber)
        : m_path(std::move(path))
        , m_isVideo(isVideo)
        , m_size(size)
        , m_owner(owner)
        , m_grabber(std::move(grabber))
    {
    }

    void run() override
    {
        const QImage image = loadOrGenerate(m_path, m_isVideo, m_size, m_grabber);
        if (image.isNull() || m_owner.isNull()) {
            return;
        }
        const QString path = m_path;
        QMetaObject::invokeMethod(m_owner.data(), [owner = m_owner, path, image]() {
            if (!owner.isNull()) {
                owner->deliver(path, image);
            }
        }, Qt::QueuedConnection);
    }

private:
    QString m_path;
    bool m_isVideo = false;
    QSize m_size;
    QPointer<ThumbnailLoader> m_owner;
    std::function<QImage(const QString&, const QSize&)> m_grabber;
};

} // namespace

ThumbnailLoader::ThumbnailLoader(QObject* parent)
    : QObject(parent)
{
    // 独立线程池：限制并发为 2，避免与视频解码争抢 CPU / 磁盘
    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(2);
}

ThumbnailLoader::~ThumbnailLoader()
{
    m_pool->clear();
    m_pool->waitForDone();
}

void ThumbnailLoader::setVideoGrabber(std::function<QImage(const QString&, const QSize&)> grabber)
{
    m_grabber = std::move(grabber);
}

QString ThumbnailLoader::cacheDir()
{
    // 路径由 AppPaths 统一管理（V4）：安装模式在 %LOCALAPPDATA%，便携模式在程序目录
    return AppPaths::thumbCacheDir();
}

int ThumbnailLoader::clearCache()
{
    const QDir dir(cacheDir());
    const QFileInfoList files = dir.entryInfoList(QStringList{QStringLiteral("*.png")}, QDir::Files);
    int removed = 0;
    for (const QFileInfo& file : files) {
        if (QFile::remove(file.absoluteFilePath())) {
            ++removed;
        }
    }
    return removed;
}

QImage ThumbnailLoader::cached(const QString& path, const QSize& size) const
{
    QImage image;
    image.load(cacheFileFor(path, size));
    return image;
}

void ThumbnailLoader::request(const QString& path, bool isVideo, const QSize& size)
{
    if (path.isEmpty() || m_pending.contains(path)) {
        return;
    }
    m_pending.insert(path);
    m_pool->start(new ThumbTask(path, isVideo, size, QPointer<ThumbnailLoader>(this), m_grabber));
}

void ThumbnailLoader::deliver(const QString& path, const QImage& image)
{
    m_pending.remove(path);
    emit ready(path, image);
}

QImage ThumbnailLoader::placeholder(const QSize& size, bool video)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    QLinearGradient gradient(0, 0, size.width(), size.height());
    if (video) {
        gradient.setColorAt(0.0, QColor(0x33, 0x41, 0x55));
        gradient.setColorAt(1.0, QColor(0x0F, 0x17, 0x2A));
    } else {
        gradient.setColorAt(0.0, QColor(0xE2, 0xE8, 0xF0));
        gradient.setColorAt(1.0, QColor(0x94, 0xA3, 0xB8));
    }

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(image.rect(), gradient);

    const int side = qMin(size.width(), size.height());
    if (video) {
        // 影片角标：圆环 + 三角
        painter.setPen(QPen(QColor(255, 255, 255, 210), qMax(2, side / 32)));
        painter.setBrush(Qt::NoBrush);
        const int r = side / 5;
        painter.drawEllipse(QPoint(size.width() / 2, size.height() / 2), r, r);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 230));
        QPolygonF triangle;
        const qreal cx = size.width() / 2.0 - r * 0.18;
        const qreal cy = size.height() / 2.0;
        triangle << QPointF(cx - r * 0.22, cy - r * 0.36) << QPointF(cx - r * 0.22, cy + r * 0.36)
                 << QPointF(cx + r * 0.42, cy);
        painter.drawPolygon(triangle);
    } else {
        painter.setPen(QPen(QColor(255, 255, 255, 170), qMax(2, side / 32)));
        painter.setBrush(Qt::NoBrush);
        const int m = side / 6;
        painter.drawRoundedRect(QRect(m, m, size.width() - 2 * m, size.height() - 2 * m), 6, 6);
    }
    painter.end();
    return image;
}
