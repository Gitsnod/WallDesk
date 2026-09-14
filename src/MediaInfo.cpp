#include "MediaInfo.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>

namespace {

struct CacheEntry {
    qint64 size = 0;
    qint64 modified = 0;
    MediaInfo info;
};

// 缓存不设上限：每条只有几十字节，一个几千项的库也就几十 KB。
// 真要清理由 clearCache() 处理（库被清空时调用）。
QHash<QString, CacheEntry> g_cache;
QMutex g_mutex;

/** 手工解析 PNG 的 IHDR（偏移 16 起的两个大端 32 位整数）。 */
bool probePng(const QString& path, MediaInfo* out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray header = file.read(24);
    file.close();
    // 8 字节签名 + 4 字节长度 + "IHDR"
    if (header.size() < 24 || !header.startsWith("\x89PNG\r\n\x1a\n")
        || header.mid(12, 4) != QByteArrayLiteral("IHDR")) {
        return false;
    }
    const auto readBe32 = [&header](int offset) -> int {
        const auto* p = reinterpret_cast<const unsigned char*>(header.constData() + offset);
        return (static_cast<int>(p[0]) << 24) | (static_cast<int>(p[1]) << 16)
               | (static_cast<int>(p[2]) << 8) | static_cast<int>(p[3]);
    };
    const int width = readBe32(16);
    const int height = readBe32(20);
    if (width <= 0 || height <= 0) {
        return false;
    }
    out->width = width;
    out->height = height;
    out->ok = true;
    return true;
}

} // namespace

MediaInfo MediaInfoProbe::probe(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return {};
    }
    const qint64 fileSize = info.size();
    const qint64 modified = info.lastModified().toMSecsSinceEpoch();

    {
        QMutexLocker locker(&g_mutex);
        const auto it = g_cache.constFind(path);
        if (it != g_cache.constEnd() && it->size == fileSize && it->modified == modified) {
            return it->info;
        }
    }

    // 缓存未命中：正常路径走 QImageReader 只读头部
    MediaInfo result;
    QImageReader reader(path);
    reader.setAutoTransform(false); // 不要为了 EXIF 旋转把整张图读进来
    const QSize dims = reader.size();
    if (dims.isValid() && dims.width() > 0 && dims.height() > 0) {
        result.width = dims.width();
        result.height = dims.height();
        result.ok = true;
    } else if (path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        // 插件失败时对 PNG 兜底：手工解析头，代价是读 24 字节
        probePng(path, &result);
    }

    {
        QMutexLocker locker(&g_mutex);
        g_cache.insert(path, CacheEntry{fileSize, modified, result});
    }
    return result;
}

void MediaInfoProbe::clearCache()
{
    QMutexLocker locker(&g_mutex);
    g_cache.clear();
}

int MediaInfoProbe::cacheSize()
{
    QMutexLocker locker(&g_mutex);
    return g_cache.size();
}

Orientation MediaInfoProbe::orientationOf(const MediaInfo& info)
{
    if (!info.valid()) {
        return Orientation::Landscape;
    }
    // 6% 容差：1920x1920 这种真方形，以及 1200x1180 这类轻微偏差都算方形
    const int diff = qAbs(info.width - info.height);
    const int longer = qMax(info.width, info.height);
    if (longer > 0 && static_cast<double>(diff) / longer <= 0.06) {
        return Orientation::Square;
    }
    return info.width > info.height ? Orientation::Landscape : Orientation::Portrait;
}

ResolutionTier MediaInfoProbe::tierOf(const MediaInfo& info)
{
    if (!info.valid()) {
        return ResolutionTier::Unknown;
    }
    // 用短边判定，这样 1920x1080 与 1080x1920 落在同一档，符合直觉
    const int shortSide = qMin(info.width, info.height);
    if (shortSide >= 2160) {
        return ResolutionTier::Uhd;
    }
    if (shortSide >= 1440) {
        return ResolutionTier::Qhd;
    }
    if (shortSide >= 1080) {
        return ResolutionTier::Fhd;
    }
    if (shortSide >= 720) {
        return ResolutionTier::Hd;
    }
    return ResolutionTier::Sd;
}

QString MediaInfoProbe::orientationLabel(Orientation value)
{
    switch (value) {
    case Orientation::Landscape:
        return QStringLiteral("横向");
    case Orientation::Portrait:
        return QStringLiteral("纵向");
    case Orientation::Square:
        return QStringLiteral("方形");
    }
    return QStringLiteral("未知");
}

QString MediaInfoProbe::tierLabel(ResolutionTier value)
{
    switch (value) {
    case ResolutionTier::Sd:
        return QStringLiteral("SD（< 720p）");
    case ResolutionTier::Hd:
        return QStringLiteral("HD（720p）");
    case ResolutionTier::Fhd:
        return QStringLiteral("FHD（1080p）");
    case ResolutionTier::Qhd:
        return QStringLiteral("QHD（2K）");
    case ResolutionTier::Uhd:
        return QStringLiteral("UHD（4K+）");
    case ResolutionTier::Unknown:
        break;
    }
    return QStringLiteral("未知");
}
