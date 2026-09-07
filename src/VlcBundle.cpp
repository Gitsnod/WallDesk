#include "VlcBundle.h"

#include <windows.h>

#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QPointer>
#include "AppPaths.h"

#include <QStandardPaths>
#include <QTimer>
#include <QtGlobal>

#include <chrono>
#include <future>
#include <memory>

#ifndef IDR_VLC_BUNDLE
#define IDR_VLC_BUNDLE 1001 // 必须与 src/VlcBundle.rc 中的定义一致
#endif

namespace {

constexpr quint32 kMagic = 0x57444231u; // "WDB1"
constexpr quint8 kFlagRaw = 0;
constexpr quint8 kFlagZlib = 1;
constexpr int kPollIntervalMs = 100;

/**
 * 缓存根目录：安装模式 %LOCALAPPDATA%/WallDesk/vlc；便携模式 <程序目录>/data/vlc。
 * 路径统一由 AppPaths 决定（V4），本模块不再关心运行模式。
 */
QString cacheRoot()
{
    return AppPaths::vlcCacheDir();
}

/** 读取嵌入当前模块的 RCDATA 资源。LockResource 不复制数据，开销可忽略。 */
QByteArray readEmbeddedBundle()
{
    const HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_VLC_BUNDLE), RT_RCDATA);
    if (!res) {
        return QByteArray();
    }
    const HGLOBAL handle = LoadResource(nullptr, res);
    if (!handle) {
        return QByteArray();
    }
    const DWORD size = SizeofResource(nullptr, res);
    const void* data = LockResource(handle);
    if (!data || size == 0) {
        return QByteArray();
    }
    return QByteArray(static_cast<const char*>(data), static_cast<int>(size));
}

struct Entry {
    QString name;      // 相对路径，posix 分隔符
    QByteArray stored; // 归档中的载荷
    quint8 flag = kFlagRaw;
    quint32 rawSize = 0;
};

/** 解析归档。只用到整数与裸字节读写，不受 QDataStream 版本差异影响。 */
bool parseBundle(const QByteArray& blob, QString* fingerprint, QList<Entry>* out, QString* err)
{
    QDataStream in(blob);
    in.setByteOrder(QDataStream::BigEndian);

    quint32 magic = 0;
    in >> magic;
    if (magic != kMagic) {
        if (err) {
            *err = QStringLiteral("内置 VLC 归档格式不正确（魔数不匹配）。");
        }
        return false;
    }

    quint32 fpLen = 0;
    in >> fpLen;
    if (fpLen > 256) {
        if (err) {
            *err = QStringLiteral("内置 VLC 归档已损坏（指纹长度异常）。");
        }
        return false;
    }
    QByteArray fpRaw(static_cast<int>(fpLen), Qt::Uninitialized);
    if (in.readRawData(fpRaw.data(), static_cast<int>(fpLen)) != static_cast<int>(fpLen)) {
        if (err) {
            *err = QStringLiteral("内置 VLC 归档已损坏（指纹读取失败）。");
        }
        return false;
    }
    *fingerprint = QString::fromUtf8(fpRaw);

    quint32 count = 0;
    in >> count;
    if (count == 0 || count > 100000) {
        if (err) {
            *err = QStringLiteral("内置 VLC 归档已损坏（文件数异常）。");
        }
        return false;
    }

    for (quint32 i = 0; i < count; ++i) {
        quint16 nameLen = 0;
        in >> nameLen;
        QByteArray nameRaw(static_cast<int>(nameLen), Qt::Uninitialized);
        if (in.readRawData(nameRaw.data(), static_cast<int>(nameLen)) != static_cast<int>(nameLen)) {
            if (err) {
                *err = QStringLiteral("内置 VLC 归档已损坏（文件名读取失败）。");
            }
            return false;
        }

        quint8 flag = kFlagRaw;
        quint32 storedSize = 0;
        quint32 rawSize = 0;
        in >> flag >> storedSize >> rawSize;
        if (storedSize > 512u * 1024u * 1024u) {
            if (err) {
                *err = QStringLiteral("内置 VLC 归档已损坏（单文件体积异常）。");
            }
            return false;
        }

        QByteArray payload(static_cast<int>(storedSize), Qt::Uninitialized);
        if (in.readRawData(payload.data(), static_cast<int>(storedSize)) != static_cast<int>(storedSize)) {
            if (err) {
                *err = QStringLiteral("内置 VLC 归档已损坏（数据读取失败）。");
            }
            return false;
        }

        Entry entry;
        entry.name = QString::fromUtf8(nameRaw);
        entry.stored = payload;
        entry.flag = flag;
        entry.rawSize = rawSize;
        out->append(entry);
    }
    return true;
}

/** 遍历目录统计字节数。 */
qint64 directorySize(const QString& path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

struct PrepareResult {
    bool ok = false;
    QString err;
};

} // namespace

bool VlcBundle::available()
{
#ifdef WALLDESK_EMBED_VLC
    return FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_VLC_BUNDLE), RT_RCDATA) != nullptr;
#else
    return false;
#endif
}

QString VlcBundle::fingerprint()
{
    QString fp;
    QList<Entry> unused;
    QString err;
    const QByteArray blob = readEmbeddedBundle();
    if (blob.isEmpty()) {
        return QString();
    }
    if (!parseBundle(blob, &fp, &unused, &err)) {
        return QString();
    }
    return fp;
}

QString VlcBundle::cacheDir()
{
    const QString fp = fingerprint();
    if (fp.isEmpty()) {
        return QString();
    }
    return cacheRoot() + QStringLiteral("/") + fp;
}

bool VlcBundle::isPrepared()
{
    const QString dir = cacheDir();
    if (dir.isEmpty()) {
        return false;
    }
    const QFileInfo stamp(dir + QStringLiteral("/.ok"));
    const QFileInfo lib(dir + QStringLiteral("/libvlc.dll"));
    return stamp.isFile() && lib.isFile();
}

bool VlcBundle::prepare(QString* err)
{
    if (!available()) {
        if (err) {
            *err = QStringLiteral("本程序未内置 VLC 运行时。");
        }
        return false;
    }
    if (isPrepared()) {
        return true;
    }

    const QString dir = cacheDir();
    if (dir.isEmpty()) {
        if (err) {
            *err = QStringLiteral("无法读取内置 VLC 运行时信息。");
        }
        return false;
    }

    QString fp;
    QList<Entry> entries;
    QString parseErr;
    if (!parseBundle(readEmbeddedBundle(), &fp, &entries, &parseErr)) {
        if (err) {
            *err = parseErr;
        }
        return false;
    }

    // 先解到临时目录，成功后整体替换，避免半包状态
    const QString tmp = dir + QStringLiteral(".tmp");
    QDir(tmp).removeRecursively();
    if (!QDir().mkpath(tmp)) {
        if (err) {
            *err = QStringLiteral("无法创建临时目录：%1").arg(tmp);
        }
        return false;
    }

    for (const Entry& entry : entries) {
        const QString target = QDir::cleanPath(tmp + QStringLiteral("/") + entry.name);
        if (!target.startsWith(tmp, Qt::CaseInsensitive)) {
            if (err) {
                *err = QStringLiteral("归档中出现非法路径：%1").arg(entry.name);
            }
            QDir(tmp).removeRecursively();
            return false;
        }
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            if (err) {
                *err = QStringLiteral("无法创建目录：%1").arg(QFileInfo(target).absolutePath());
            }
            QDir(tmp).removeRecursively();
            return false;
        }

        const QByteArray data = (entry.flag == kFlagZlib) ? qUncompress(entry.stored) : entry.stored;
        if (static_cast<quint32>(data.size()) != entry.rawSize) {
            if (err) {
                *err = QStringLiteral("内置文件解压失败：%1").arg(entry.name);
            }
            QDir(tmp).removeRecursively();
            return false;
        }

        QFile out(target);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) {
            if (err) {
                *err = QStringLiteral("无法写入文件：%1").arg(target);
            }
            out.close();
            QDir(tmp).removeRecursively();
            return false;
        }
    }

    QDir(dir).removeRecursively();
    if (!QDir().rename(tmp, dir)) {
        if (err) {
            *err = QStringLiteral("无法安装 VLC 运行时到：%1").arg(dir);
        }
        QDir(tmp).removeRecursively();
        return false;
    }

    QFile stamp(dir + QStringLiteral("/.ok"));
    if (stamp.open(QIODevice::WriteOnly)) {
        stamp.write(fp.toUtf8());
    }
    return true;
}

void VlcBundle::prepareAsync(QObject* context,
                             std::function<void(bool ok, const QString& err)> callback)
{
    if (!context) {
        return;
    }

    auto future = std::make_shared<std::future<PrepareResult>>(std::async(std::launch::async, [] {
        QString err;
        const bool ok = VlcBundle::prepare(&err);
        return PrepareResult{ok, err};
    }));

    QPointer<QObject> guard(context);
    auto* timer = new QTimer(context);
    QObject::connect(timer, &QTimer::timeout, context,
                     [guard, future, timer, callback]() {
                         if (guard.isNull()) {
                             timer->deleteLater();
                             return;
                         }
                         if (future->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                             return;
                         }
                         const PrepareResult result = future->get();
                         timer->stop();
                         timer->deleteLater();
                         if (!guard.isNull()) {
                             callback(result.ok, result.err);
                         }
                     });
    timer->start(kPollIntervalMs);
}

QString VlcBundle::runtimeDir()
{
    return isPrepared() ? cacheDir() : QString();
}

qint64 VlcBundle::purgeStaleCaches()
{
    const QDir root(cacheRoot());
    if (!root.exists()) {
        return 0;
    }
    const QString keep = fingerprint();
    qint64 freed = 0;
    for (const QFileInfo& dir : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (dir.fileName() == keep) {
            continue;
        }
        freed += directorySize(dir.absoluteFilePath());
        QDir(dir.absoluteFilePath()).removeRecursively();
    }
    return freed;
}
