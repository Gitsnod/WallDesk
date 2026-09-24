#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * 在线壁纸源（V4.7 新增）。
 *
 * 现状问题：所有壁纸必须用户先在浏览器里存到本地再「添加图片」，
 * 上手门槛高，也让「每天换一张」这种玩法没法自然实现。
 *
 * 提供两种能力：
 *   1. 从 URL 直接导入图片到壁纸库（落到 数据目录/downloads）；
 *   2. Bing 每日一图：请求其官方 JSON 接口拿当日图，再下原图。
 *
 * 网络访问全部异步，不阻塞界面；每个请求都有超时与大小上限，
 * 避免被异常大的响应体拖死。
 */
class OnlineSources : public QObject {
    Q_OBJECT
public:
    explicit OnlineSources(QObject* parent = nullptr);
    ~OnlineSources() override;

    /** 下载图片文件的目录（不存在时自动创建）。 */
    static QString downloadDir();

    /** Bing 每日一图接口：返回当天图片的元信息与图片直链。 */
    static QString bingApiUrl();

    /**
     * 从任意 URL 下载一张图片。
     * 下载目录由 downloadDir() 决定，文件名按「域名_原文件名」生成（见 fileNameFor）。
     * 若目标内容在目录里已存在，直接复用已有文件，不会重复落盘。
     */
    void downloadImage(const QString& url);

    /** 下载 Bing 每日一图。可选指定第 n 天前（0 = 今天，最多 7）。 */
    void downloadBingDaily(int daysAgo = 0);

    bool busy() const { return m_active > 0; }

    /** 单张图片大小上限（超过即中断，防呆）。 */
    static qint64 maxImageBytes();
    /** 请求超时（毫秒）。 */
    static int timeoutMs();

    /**
     * 由 URL 推导落盘文件名（去查询串、补扩展名、防路径穿越）。
     * 公开出来是为了能被独立单元测试覆盖——它是纯函数，没有副作用。
     *
     * label 用来直接指定文件名主干（Bing 每日一图传 "bing_日期_图片名"）。
     * label 为空时按「域名_原文件名」拼；路径里没有可用文件名（Bing 的
     * /th?id=... 就是这种）时退化为「域名_短哈希」。两种情况都比原来的
     * 「wall_<URL哈希>」可读——用户至少认得出这是哪张图、从哪来。
     */
    static QString fileNameFor(const QString& url, const QString& contentType,
                               const QString& label = QString());
    /**
     * 在 dir 下按内容找出与 payload 完全相同的文件，返回绝对路径；没有则返回空。
     * 只读、无副作用。公开是为了能被独立单元测试覆盖——它是「同一张图重复下载
     * 不堆副本」这条保证的唯一判据，值得有确定性测试兜住。
     */
    static QString findDuplicateByContent(const QString& dir, const QByteArray& payload);
    /** 校验 URL 是否是可接受的 http/https 地址。 */
    static bool isAcceptableUrl(const QUrl& url, QString* reason = nullptr);

signals:
    /** 一张图片已就绪（落盘完成）。 */
    void imageReady(const QString& localPath);
    /** 进度：0..100，total 为 0 表示未知大小。 */
    void progress(qint64 received, qint64 total);
    /** 失败，reason 为可直接展示给用户的说明。 */
    void failed(const QString& reason);
    /** 一批请求全部结束（成功或失败）。 */
    void finished();

private slots:
    void onImageReplyFinished(QNetworkReply* reply);
    void onBingMetaFinished(QNetworkReply* reply);

private:
    /** 发起一次图片请求；label 参与最终落盘文件名（见 fileNameFor）。 */
    void requestImage(const QString& url, const QString& label);
    void beginRequest();
    void endRequest();
    /** 落盘并校验，成功返回本地路径。命中已有同内容文件时直接复用，不写新副本。 */
    QString storePayload(const QByteArray& payload, const QString& url, const QString& contentType,
                         const QString& label, QString* err);

    QNetworkAccessManager* m_net = nullptr;
    int m_active = 0;       // 在途请求数，用于 busy()/finished()
    int m_batchTotal = 0;   // 当前批次请求总数
    int m_batchDone = 0;
};
