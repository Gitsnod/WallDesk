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
#include <QRadialGradient>
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
constexpr int kMaxCacheFiles = 400;
constexpr int kTrimCheckEvery = 50;

/** 内存 LRU 上限（张数）。160x100 的 ARGB 图约 64 KB，180 张约 11 MB。 */
constexpr int kMaxMemoryThumbs = 180;

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
        if (m_owner.isNull()) {
            return;
        }
        // 失败也要回传一次：否则该路径会一直留在 pending 里，之后再也不会重试
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
    , m_memory(kMaxMemoryThumbs)
{
    // 独立线程池：限制并发为 2，避免与视频解码争抢 CPU / 磁盘
    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(2);
    // 视频抓帧串行：每次抓帧都要新建一个 libvlc 实例，并行会让内存峰值翻倍
    m_videoPool = new QThreadPool(this);
    m_videoPool->setMaxThreadCount(1);
}

ThumbnailLoader::~ThumbnailLoader()
{
    m_pool->clear();
    m_pool->waitForDone();
    m_videoPool->clear();
    m_videoPool->waitForDone();
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

bool ThumbnailLoader::memoryPixmap(const QString& path, QPixmap* out) const
{
    if (path.isEmpty() || !out) {
        return false;
    }
    if (QPixmap* hit = m_memory.object(path)) {
        *out = *hit;
        return true;
    }
    return false;
}

void ThumbnailLoader::request(const QString& path, bool isVideo, const QSize& size)
{
    if (path.isEmpty() || m_pending.contains(path)) {
        return;
    }
    // 已在内存里就没必要再排队
    if (m_memory.contains(path)) {
        return;
    }
    m_pending.insert(path);
    auto* task = new ThumbTask(path, isVideo, size, QPointer<ThumbnailLoader>(this), m_grabber);
    (isVideo ? m_videoPool : m_pool)->start(task);
}

void ThumbnailLoader::clearPending()
{
    m_pending.clear();
    m_pool->clear();
    m_videoPool->clear();
}

void ThumbnailLoader::clearMemory()
{
    m_memory.clear();
}

void ThumbnailLoader::deliver(const QString& path, const QImage& image)
{
    m_pending.remove(path);
    if (image.isNull()) {
        return; // 生成失败：交回给调用方继续画占位图
    }
    m_memory.insert(path, new QPixmap(QPixmap::fromImage(image)), 1);
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

    // 只画一层很淡的中心光斑，具体角标（播放三角）由列表委托统一叠加，
    // 这样真实缩略图与占位图的角标位置完全一致，不会出现「有没有角标」的跳变。
    const int side = qMin(size.width(), size.height());
    QRadialGradient glow(QPointF(size.width() / 2.0, size.height() / 2.0), side * 0.7);
    glow.setColorAt(0.0, video ? QColor(0x60, 0xA5, 0xFA, 40) : QColor(0xFF, 0xFF, 0xFF, 40));
    glow.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.fillRect(image.rect(), glow);
    painter.end();
    return image;
}
