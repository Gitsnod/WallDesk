#include "Logger.h"

#include "AppPaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QtGlobal>

#include <windows.h>

namespace {
QMutex g_mutex;
QString g_path;
bool g_verbose = false;
/** 单文件上限，超过则滚动。 */
constexpr qint64 kMaxFileSize = 2 * 1024 * 1024;

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DBG";
    case QtInfoMsg:     return "INF";
    case QtWarningMsg:  return "WRN";
    case QtCriticalMsg: return "ERR";
    case QtFatalMsg:    return "FAT";
    }
    return "???";
}

/** 日志滚动：WallDesk.log -> WallDesk.1.log -> WallDesk.2.log（更旧的丢弃）。 */
void rollIfNeeded(const QString& path)
{
    if (!QFileInfo::exists(path) || QFileInfo(path).size() < kMaxFileSize) {
        return;
    }
    QFile::remove(path + QStringLiteral(".2.log"));
    QFile::rename(path + QStringLiteral(".1.log"), path + QStringLiteral(".2.log"));
    QFile::rename(path, path + QStringLiteral(".1.log"));
}

/** 取系统版本描述，便于判断「在对方的 Windows 上是否跑得起来」。 */
QString windowsVersionText()
{
    const HKEY kRegKey = HKEY_LOCAL_MACHINE;
    HKEY key = nullptr;
    static const wchar_t kSubKey[] =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    QString name = QStringLiteral("Windows");
    QString build;
    if (RegOpenKeyExW(kRegKey, kSubKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t buffer[256] = {0};
        DWORD size = sizeof(buffer);
        if (RegQueryValueExW(key, L"ProductName", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS) {
            name = QString::fromWCharArray(buffer);
        }
        size = sizeof(buffer);
        memset(buffer, 0, sizeof(buffer));
        if (RegQueryValueExW(key, L"CurrentBuildNumber", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS) {
            build = QString::fromWCharArray(buffer);
        }
        RegCloseKey(key);
    }
    return build.isEmpty() ? name : QStringLiteral("%1 (build %2)").arg(name, build);
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (!g_verbose && (type == QtDebugMsg || type == QtInfoMsg)) {
        return; // 发行版默认不记录调试与一般信息，避免日志膨胀
    }
    QString text = message;
    if (context.category && qstrcmp(context.category, "default") != 0) {
        text = QStringLiteral("[%1] %2").arg(QString::fromLatin1(context.category), message);
    }
    Logger::write(type, text);
    if (type == QtFatalMsg) {
        DebugBreak(); // 调试器下直接中断；无调试器时该异常被忽略，进程继续退出
    }
}
}

void Logger::write(QtMsgType type, const QString& message)
{
    QMutexLocker locker(&g_mutex);
    if (g_path.isEmpty()) {
        return;
    }
    QDir().mkpath(logDir());
    rollIfNeeded(g_path);

    QFile file(g_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
           << QStringLiteral(" [") << QString::fromLatin1(levelName(type)) << QStringLiteral("] ")
           << message << Qt::endl;
    file.close();
}

void Logger::install(bool verbose)
{
    g_verbose = verbose;
    g_path = logFilePath();
    QDir().mkpath(logDir());
    qInstallMessageHandler(messageHandler);

    info(QStringLiteral("==== WallDesk %1 启动 ====").arg(QCoreApplication::applicationVersion()));
    info(QStringLiteral("Qt %1（编译期 %2）  运行模式：%3")
             .arg(QString::fromLatin1(qVersion()), QString::fromLatin1(QT_VERSION_STR),
                  AppPaths::modeText()));
    info(QStringLiteral("系统：%1").arg(windowsVersionText()));
    info(QStringLiteral("程序目录：%1").arg(AppPaths::appDir()));
    info(QStringLiteral("数据目录：%1").arg(AppPaths::dataDir()));
}

QString Logger::logDir()
{
    return AppPaths::logDir();
}

QString Logger::logFilePath()
{
    return AppPaths::logDir() + QStringLiteral("/WallDesk.log");
}

void Logger::info(const QString& message)
{
    write(QtInfoMsg, message);
}

void Logger::warning(const QString& message)
{
    write(QtWarningMsg, message);
}

void Logger::critical(const QString& message)
{
    write(QtCriticalMsg, message);
}
