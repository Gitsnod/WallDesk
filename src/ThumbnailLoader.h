#pragma once

#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <functional>

class QThreadPool;

/**
 * 缩略图加载器：磁盘缓存 + 线程池异步解码。
 *
 * 必要性：清单里常有大图（单张 5-20 MB），若在主线程同步解码上百张，
 * 界面会明显卡顿。这里把解码放进线程池，并把结果缓存成 png，
 * 二次启动直接读缓存，几乎零开销。
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
    /** 请求缩略图，结果经 ready() 异步返回。同一路径的重复请求会被合并。 */
    void request(const QString& path, bool isVideo, const QSize& size);

    /** 生成占位图（解码失败或视频抓帧失败时使用）。 */
    static QImage placeholder(const QSize& size, bool video);

    static QString cacheDir();
    /** 清空磁盘缓存，返回删除的文件数。 */
    static int clearCache();

    /** 供工作线程回传结果，内部使用。 */
    void deliver(const QString& path, const QImage& image);

signals:
    void ready(const QString& path, const QImage& image);

private:
    QThreadPool* m_pool = nullptr;
    std::function<QImage(const QString&, const QSize&)> m_grabber;
    QSet<QString> m_pending;
};
