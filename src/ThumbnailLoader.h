#pragma once

#include <QCache>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <functional>

class QThreadPool;

/**
 * 缩略图加载器：内存 LRU + 磁盘缓存 + 线程池异步解码。
 *
 * 必要性：清单里常有大图（单张 5-20 MB），若在主线程同步解码上百张，
 * 界面会明显卡顿。这里把解码放进线程池，并把结果缓存成 png，
 * 二次启动直接读缓存，几乎零开销。
 *
 * V4.5 内存优化：
 *   1. 解码结果除写磁盘外，还放进有上限的内存 LRU（默认 180 张），
 *      列表项本身不再持有 QPixmap，几百项的库内存占用从几十 MB 降到几 MB；
 *   2. 只对可见区域发起请求（调用方配合），滚动时才补充加载；
 *   3. 视频抓帧走独立的单线程池——每个抓帧都要起一个 libvlc 实例，
 *      并行会让内存瞬间翻倍，串行反而更快也更稳。
 *
 * 视频缩略图依赖外部注入的抓帧函数（由 VlcPlayer 提供），
 * 未注入或抓帧失败时自动降级为绘制的占位图，不影响列表展示。
 */
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject* parent = nullptr);
    ~ThumbnailLoader() override;

    /** 注入视频抓帧实现；签名：文件路径 + 目标尺寸 → 图像。 */
    void setVideoGrabber(std::function<QImage(const QString&, const QSize&)> grabber);

    /** 同步查询磁盘缓存。命中返回图像，未命中返回空图像。 */
    QImage cached(const QString& path, const QSize& size) const;
    /** 查询内存 LRU。命中写入 out 并返回 true；不命中返回 false（调用方应发请求）。 */
    bool memoryPixmap(const QString& path, QPixmap* out) const;
    /** 请求缩略图，结果经 ready() 异步返回。同一路径的重复请求会被合并。 */
    void request(const QString& path, bool isVideo, const QSize& size);
    /** 放弃在途的排队任务（列表整体刷新时调用，避免为已移除的项白做工）。 */
    void clearPending();

    /** 生成占位图（解码失败或视频抓帧失败时使用）。 */
    static QImage placeholder(const QSize& size, bool video);

    static QString cacheDir();
    /** 清空磁盘缓存，返回删除的文件数。 */
    static int clearCache();
    /** 清空内存 LRU，立即释放已解码的缩略图。 */
    void clearMemory();

    /** 供工作线程回传结果，内部使用。 */
    void deliver(const QString& path, const QImage& image);

signals:
    void ready(const QString& path, const QImage& image);

private:
    QThreadPool* m_pool = nullptr;      // 图片解码：2 线程
    QThreadPool* m_videoPool = nullptr; // 视频抓帧：串行
    std::function<QImage(const QString&, const QSize&)> m_grabber;
    QSet<QString> m_pending;
    QCache<QString, QPixmap> m_memory;
};
