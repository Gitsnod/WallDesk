#include "OnlineSources.h"

#include "AppPaths.h"
#include "Logger.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QUrlQuery>

namespace {

/** 允许的图片扩展名（也用于从 URL 猜后缀）。 */
const QStringList kImageSuffixes = {QStringLiteral("jpg"),  QStringLiteral("jpeg"),
                                    QStringLiteral("png"),  QStringLiteral("bmp"),
                                    QStringLiteral("webp"), QStringLiteral("gif")};

QString suffixFromContentType(const QString& contentType)
{
    const QString lowered = contentType.toLower();
    if (lowered.contains(QStringLiteral("jpeg")) || lowered.contains(QStringLiteral("jpg"))) {
        return QStringLiteral("jpg");
    }
    if (lowered.contains(QStringLiteral("png"))) {
        return QStringLiteral("png");
    }
    if (lowered.contains(QStringLiteral("bmp"))) {
        return QStringLiteral("bmp");
    }
    if (lowered.contains(QStringLiteral("webp"))) {
        return QStringLiteral("webp");
    }
    if (lowered.contains(QStringLiteral("gif"))) {
        return QStringLiteral("gif");
    }
    return {};
}

} // namespace

OnlineSources::OnlineSources(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

OnlineSources::~OnlineSources() = default;

QString OnlineSources::downloadDir()
{
    return QDir(AppPaths::dataDir()).filePath(QStringLiteral("downloads"));
}

QString OnlineSources::bingApiUrl()
{
    // idx=0 为当天；接口固定返回 8 条历史，够用
    return QStringLiteral("https://www.bing.com/HPImageArchive.aspx"
                          "?format=js&idx=0&n=8&mkt=zh-CN");
}

qint64 OnlineSources::maxImageBytes()
{
    return 40LL * 1024 * 1024; // 40 MB：4K 无损 PNG 也够，再大基本是误抓
}

int OnlineSources::timeoutMs()
{
    return 20000;
}

bool OnlineSources::isAcceptableUrl(const QUrl& url, QString* reason)
{
    if (!url.isValid() || url.host().isEmpty()) {
        if (reason) {
            *reason = QStringLiteral("网址格式不正确");
        }
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        if (reason) {
            *reason = QStringLiteral("只支持 http / https 地址");
        }
        return false;
    }
    return true;
}

QString OnlineSources::fileNameFor(const QString& url, const QString& contentType)
{
    const QUrl parsed(url);
    QString name = QFileInfo(parsed.path()).fileName();

    // 路径里没有可用文件名（或以查询串为主）时，用 URL 哈希兜底
    auto looksLikeImage = [](const QString& candidate) {
        const QString suffix = QFileInfo(candidate).suffix().toLower();
        return !suffix.isEmpty() && kImageSuffixes.contains(suffix);
    };
    if (name.isEmpty() || !looksLikeImage(name)) {
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex().left(16));
        QString suffix = QFileInfo(name).suffix().toLower();
        if (!kImageSuffixes.contains(suffix)) {
            suffix = suffixFromContentType(contentType);
        }
        if (suffix.isEmpty()) {
            suffix = QStringLiteral("jpg");
        }
        name = QStringLiteral("wall_%1.%2").arg(hash, suffix);
    }

    // 去掉可能混进来的路径分隔符，只保留纯文件名
    name = QFileInfo(name).fileName();
    // 文件名过长会让某些文件系统报错，截断但保留扩展名
    if (name.size() > 120) {
        const QString suffix = QFileInfo(name).suffix();
        name = name.left(100) + QStringLiteral(".") + suffix;
    }
    return name;
}

void OnlineSources::beginRequest()
{
    if (m_active == 0) {
        m_batchTotal = 0;
        m_batchDone = 0;
    }
    ++m_active;
    ++m_batchTotal;
}

void OnlineSources::endRequest()
{
    --m_active;
    ++m_batchDone;
    if (m_active <= 0) {
        m_active = 0;
        emit finished();
    }
}

QString OnlineSources::storePayload(const QByteArray& payload, const QString& url,
                                    const QString& contentType, QString* err)
{
    if (payload.isEmpty()) {
        if (err) {
            *err = QStringLiteral("下载内容为空");
        }
        return {};
    }

    const QString dir = downloadDir();
    if (!QDir().mkpath(dir)) {
        if (err) {
            *err = QStringLiteral("无法创建下载目录：%1").arg(dir);
        }
        return {};
    }

    QString name = fileNameFor(url, contentType);
    QString target = QDir(dir).filePath(name);

    // 同名不覆盖：追加序号，避免把用户之前下的图冲掉
    if (QFileInfo::exists(target)) {
        const QString base = QFileInfo(name).completeBaseName();
        const QString suffix = QFileInfo(name).suffix();
        for (int i = 1; i < 1000; ++i) {
            const QString candidate =
                QDir(dir).filePath(QStringLiteral("%1_%2.%3").arg(base).arg(i).arg(suffix));
            if (!QFileInfo::exists(candidate)) {
                target = candidate;
                break;
            }
        }
    }

    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        if (err) {
            *err = QStringLiteral("无法写入文件：%1").arg(file.errorString());
        }
        return {};
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
        if (err) {
            *err = QStringLiteral("写入文件失败：%1").arg(file.errorString());
        }
        return {};
    }
    return QDir::toNativeSeparators(target);
}

void OnlineSources::downloadImage(const QString& url)
{
    QString reason;
    const QUrl parsed(url.trimmed());
    if (!isAcceptableUrl(parsed, &reason)) {
        emit failed(reason);
        return;
    }

    beginRequest();

    QNetworkRequest request(parsed);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("WallDesk/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(timeoutMs());

    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onImageReplyFinished(reply);
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit progress(received, total); });
}

void OnlineSources::downloadBingDaily(int daysAgo)
{
    const int index = qBound(0, daysAgo, 7);
    beginRequest();

    QUrl api(bingApiUrl());
    QNetworkRequest request(api);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("WallDesk/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(timeoutMs());

    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, index]() {
        reply->setProperty("bingIndex", index);
        onBingMetaFinished(reply);
    });
}

void OnlineSources::onBingMetaFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        endRequest();
        emit failed(QStringLiteral("获取 Bing 每日一图失败：%1").arg(reply->errorString()));
        return;
    }

    const int index = reply->property("bingIndex").toInt();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        endRequest();
        emit failed(QStringLiteral("Bing 接口返回无法解析（%1）").arg(parseError.errorString()));
        return;
    }

    const QJsonArray images = doc.object().value(QStringLiteral("images")).toArray();
    if (images.isEmpty()) {
        endRequest();
        emit failed(QStringLiteral("Bing 接口没有返回图片列表"));
        return;
    }

    const QJsonObject entry = images.at(qBound(0, index, images.size() - 1)).toObject();
    QString relative = entry.value(QStringLiteral("url")).toString();
    if (relative.isEmpty()) {
        endRequest();
        emit failed(QStringLiteral("Bing 接口未包含图片地址"));
        return;
    }
    // 接口给的是 /th?id=... 形式的相对路径，且默认是缩略图；
    // 换成 UHD 版本拿原图（Bing 对同一 id 支持 _UHD 变体）
    relative.replace(QStringLiteral("_1920x1080"), QStringLiteral("_UHD"));
    const QUrl imageUrl(QStringLiteral("https://www.bing.com") + relative);
    Logger::info(QStringLiteral("Bing 每日一图：%1").arg(imageUrl.toString()));

    // 元信息请求结束，图片请求作为同一批次的一部分继续进行
    endRequest();
    downloadImage(imageUrl.toString());
}

void OnlineSources::onImageReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    // 提前中断过大的响应体：Content-Length 已知时先判断，未知时边收边查
    const QVariant declaredLength = reply->header(QNetworkRequest::ContentLengthHeader);
    if (declaredLength.isValid() && declaredLength.toLongLong() > maxImageBytes()) {
        reply->abort();
        endRequest();
        emit failed(QStringLiteral("图片太大（%1 MB），已跳过")
                        .arg(declaredLength.toLongLong() / (1024 * 1024)));
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        endRequest();
        emit failed(QStringLiteral("下载失败：%1").arg(reply->errorString()));
        return;
    }

    const QByteArray payload = reply->readAll();
    if (payload.size() > maxImageBytes()) {
        endRequest();
        emit failed(QStringLiteral("图片太大（%1 MB），已跳过")
                        .arg(payload.size() / (1024 * 1024)));
        return;
    }

    const QString contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString();
    // 有些站点对图片也返回 text/html（反盗链页），拦下来给个明确提示
    if (contentType.startsWith(QStringLiteral("text/")) && payload.size() < 4096) {
        endRequest();
        emit failed(QStringLiteral("该地址返回的不是图片（可能是网页或需要登录）"));
        return;
    }

    QString err;
    const QString local = storePayload(payload, reply->url().toString(), contentType, &err);
    endRequest();
    if (local.isEmpty()) {
        emit failed(err.isEmpty() ? QStringLiteral("保存图片失败") : err);
        return;
    }
    Logger::info(QStringLiteral("在线图片已保存：%1").arg(local));
    emit imageReady(local);
}
