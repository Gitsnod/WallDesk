#include "OnlineSources.h"

#include "AppPaths.h"
#include "Logger.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDate>
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

/**
 * 拼文件名用的净化：只留 ASCII 字母数字与 - _，其余一律压成下划线。
 * 中文与空格在文件名里能存但不好认也不好搜，且跨系统传输容易出问题。
 */
QString sanitizeNamePart(const QString& raw)
{
    QString out;
    out.reserve(raw.size());
    for (const QChar& ch : raw) {
        const bool ascii = ch.unicode() < 128;
        if (ascii && ch.isLetterOrNumber()) {
            out.append(ch);
        } else if (ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            out.append(ch);
        } else {
            out.append(QLatin1Char('_'));
        }
    }
    out.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    while (out.startsWith(QLatin1Char('_'))) {
        out.remove(0, 1);
    }
    while (out.endsWith(QLatin1Char('_'))) {
        out.chop(1);
    }
    return out;
}

/** 域名 → 文件名片段（去掉 www.，点换成下划线）。 */
QString hostPart(const QUrl& url)
{
    QString host = url.host().toLower();
    if (host.startsWith(QStringLiteral("www."))) {
        host.remove(0, 4);
    }
    return sanitizeNamePart(host);
}

/**
 * Bing 每日一图的文件名主干：bing_<日期>_<图片名>。
 *
 * 日期取接口返回的 startdate（不是本机当天）——index 可以往前取历史几天，
 * 用本机日期会把历史图错标成今天。图片名从地址里的 id 抠：
 * /th?id=OHR.ElGolfo_ZH-CN8329995759_UHD.jpg → ElGolfo。
 */
QString bingLabel(const QJsonObject& entry, const QString& imageUrl)
{
    QString date = entry.value(QStringLiteral("startdate")).toString();
    const QDate parsed = QDate::fromString(date, QStringLiteral("yyyyMMdd"));
    date = parsed.isValid() ? parsed.toString(QStringLiteral("yyyy-MM-dd"))
                            : QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    const QRegularExpression re(QStringLiteral("OHR\\.([A-Za-z0-9]+)_"));
    const QRegularExpressionMatch match = re.match(imageUrl);
    const QString slug = match.hasMatch() ? match.captured(1) : QString();
    return slug.isEmpty() ? QStringLiteral("bing_%1").arg(date)
                          : QStringLiteral("bing_%1_%2").arg(date, slug);
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

QString OnlineSources::fileNameFor(const QString& url, const QString& contentType,
                                   const QString& label)
{
    const QUrl parsed(url);

    auto looksLikeImage = [](const QString& candidate) {
        const QString suffix = QFileInfo(candidate).suffix().toLower();
        return !suffix.isEmpty() && kImageSuffixes.contains(suffix);
    };

    // ---- 文件名主干 ----
    QString stem;
    if (!label.isEmpty()) {
        // 调用方直接给了主干（Bing 每日一图）——最可读，优先用
        stem = sanitizeNamePart(label);
    }
    if (stem.isEmpty()) {
        // 「域名_原文件名」：既认得出是哪张图，也认得出从哪来
        const QString host = hostPart(parsed);
        const QString base = QFileInfo(parsed.path()).fileName();
        if (looksLikeImage(base)) {
            const QString readable = sanitizeNamePart(QFileInfo(base).completeBaseName());
            stem = readable.isEmpty() ? host : QStringLiteral("%1_%2").arg(host, readable);
        }
        if (stem.isEmpty()) {
            // 路径里没有可读文件名（Bing 的 /th?id=... 正是这种）——退到短哈希
            const QString hash = QString::fromLatin1(
                QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex().left(8));
            stem = host.isEmpty() ? QStringLiteral("wall_%1").arg(hash)
                                  : QStringLiteral("%1_%2").arg(host, hash);
        }
    }

    // ---- 扩展名：优先信 URL 后缀，其次信响应头 ----
    QString suffix = QFileInfo(parsed.path()).suffix().toLower();
    if (!kImageSuffixes.contains(suffix)) {
        suffix = suffixFromContentType(contentType);
    }
    if (suffix.isEmpty()) {
        suffix = QStringLiteral("jpg");
    }

    // 文件名过长会让某些文件系统报错，截断但保留扩展名
    const int maxStem = 120 - (suffix.size() + 1);
    if (maxStem > 0 && stem.size() > maxStem) {
        stem = stem.left(maxStem);
    }
    return stem + QLatin1Char('.') + suffix;
}

QString OnlineSources::findDuplicateByContent(const QString& dir, const QByteArray& payload)
{
    const QByteArray wanted = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    const QFileInfoList entries =
        QDir(dir).entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& info : entries) {
        // 先按文件大小筛掉绝大多数候选，只对「大小完全相同」的极少数文件算摘要。
        // 否则每下完一张图都要把整个 downloads 目录读一遍，库大了就变成拖累。
        if (info.size() != payload.size()) {
            continue;
        }
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256) == wanted) {
            return info.absoluteFilePath();
        }
    }
    return {};
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
                                    const QString& contentType, const QString& label, QString* err)
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

    // 内容去重：同一张图再下一次时复用它，而不是再堆一个副本。
    // 触发场景很常见——同一天点两回「Bing 每日一图」，或同一个地址换个
    // 查询串再贴一遍。此前只做「同名不覆盖」，于是每次点击都多一个 _1 文件，
    // 库里也跟着多一条一模一样的条目。
    const QString existing = findDuplicateByContent(dir, payload);
    if (!existing.isEmpty()) {
        Logger::info(QStringLiteral("下载内容与已有文件相同，直接复用：%1")
                         .arg(QDir::toNativeSeparators(existing)));
        return QDir::toNativeSeparators(existing);
    }

    const QString name = fileNameFor(url, contentType, label);
    QString target = QDir(dir).filePath(name);

    // 到这里说明是「同名但内容不同」，才追加序号，避免把之前下的图冲掉
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
    requestImage(url, QString());
}

void OnlineSources::requestImage(const QString& url, const QString& label)
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
    // 文件名主干要一路带到落盘那一步（Bing 传的是 bing_日期_图片名）
    reply->setProperty("nameLabel", label);
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

    // Bing 的地址里带着图 id，同一张图的文件名可以提前算出来。已经下过就直接
    // 复用——连这次流量一起省掉（此前是老老实实把 3.7 MB 再拉一遍，再存成
    // 一个 _1 副本，同一天点两次就是两份）。
    const QString label = bingLabel(entry, imageUrl.toString());
    const QString precomputed =
        QDir(downloadDir()).filePath(fileNameFor(imageUrl.toString(), QString(), label));
    if (QFileInfo::exists(precomputed)) {
        Logger::info(QStringLiteral("Bing 每日一图已存在，直接复用：%1")
                         .arg(QDir::toNativeSeparators(precomputed)));
        emit imageReady(QDir::toNativeSeparators(precomputed));
        endRequest();
        return;
    }

    // 元信息请求结束，图片请求作为同一批次的一部分继续进行
    endRequest();
    requestImage(imageUrl.toString(), label);
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
    const QString label = reply->property("nameLabel").toString();
    const QString local =
        storePayload(payload, reply->url().toString(), contentType, label, &err);
    endRequest();
    if (local.isEmpty()) {
        emit failed(err.isEmpty() ? QStringLiteral("保存图片失败") : err);
        return;
    }
    Logger::info(QStringLiteral("在线图片已保存：%1").arg(local));
    emit imageReady(local);
}
