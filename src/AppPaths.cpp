#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSettings>
#include <QStandardPaths>

namespace {
/** 便携模式标记文件名。存在即启用，内容可以是任意说明文字。 */
const char* kPortableFlag = "portable.ini";
const char* kPortableStub =
    "; WallDesk 便携模式标记文件\n"
    "; 本文件存在时，配置与数据都保存在程序同目录的 data\\ 下，不写注册表。\n"
    "; 删除本文件即可恢复为安装模式（配置写回注册表，原便携配置保留在 data\\ 目录）。\n";
}

AppPaths& AppPaths::inst()
{
    static AppPaths self;
    return self;
}

void AppPaths::initialize(const QString& appDir, bool forcePortable)
{
    AppPaths& self = inst();
    self.m_appDir = appDir;

    const QString flag = appDir + QStringLiteral("/") + QString::fromLatin1(kPortableFlag);
    self.m_mode = (forcePortable || QFileInfo::exists(flag)) ? Mode::Portable : Mode::Installed;

    if (self.m_mode == Mode::Portable) {
        self.m_dataDir = appDir + QStringLiteral("/data");
    } else {
        self.m_dataDir =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    QDir().mkpath(self.m_dataDir);
    QDir().mkpath(logDir());
}

AppPaths::Mode AppPaths::mode()
{
    return inst().m_mode;
}

bool AppPaths::portable()
{
    return inst().m_mode == Mode::Portable;
}

QString AppPaths::modeText()
{
    return portable() ? QStringLiteral("便携模式（配置与数据保存在程序目录）")
                      : QStringLiteral("安装模式（配置保存在注册表）");
}

QString AppPaths::appDir()
{
    return inst().m_appDir;
}

QString AppPaths::dataDir()
{
    return inst().m_dataDir;
}

QString AppPaths::logDir()
{
    return inst().m_dataDir + QStringLiteral("/logs");
}

QString AppPaths::vlcCacheDir()
{
    return inst().m_dataDir + QStringLiteral("/vlc");
}

QString AppPaths::thumbCacheDir()
{
    return inst().m_dataDir + QStringLiteral("/thumbs");
}

QString AppPaths::configFile()
{
    if (portable()) {
        // Qt 的 IniFormat + UserScope 会在指定目录下再建一层组织名目录
        return dataDir() + QStringLiteral("/") + QCoreApplication::organizationName()
               + QStringLiteral("/") + QCoreApplication::applicationName()
               + QStringLiteral(".ini");
    }
    return QStringLiteral(R"(HKEY_CURRENT_USER\Software\)") + QCoreApplication::organizationName()
           + QStringLiteral("\\") + QCoreApplication::applicationName();
}

QString AppPaths::portableFlagFile()
{
    return appDir() + QStringLiteral("/") + QString::fromLatin1(kPortableFlag);
}

void AppPaths::applySettingsFormat()
{
    if (!portable()) {
        return; // 安装模式保持平台默认（Windows 注册表）
    }
    QDir().mkpath(dataDir());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dataDir());
}

bool AppPaths::setPortable(bool enabled, QString* err)
{
    const QString flag = portableFlagFile();
    if (enabled) {
        QFile file(flag);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (err) {
                *err = QStringLiteral("无法写入 %1：%2").arg(flag, file.errorString());
            }
            return false;
        }
        file.write(kPortableStub);
        file.close();
        return true;
    }
    if (QFileInfo::exists(flag) && !QFile::remove(flag)) {
        if (err) {
            *err = QStringLiteral("无法删除 %1，请检查程序目录是否可写（可能需要管理员权限）。")
                       .arg(flag);
        }
        return false;
    }
    return true;
}
