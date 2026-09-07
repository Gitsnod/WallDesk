#pragma once

#include <QString>

/**
 * 应用路径与运行模式（V4 新增，面向分发）。
 *
 * 两种运行模式：
 *   1. 安装模式（默认）：配置写注册表 HKCU\Software\WallDesk，
 *      数据（日志、VLC 运行时、缩略图缓存）写 %LOCALAPPDATA%\WallDesk。
 *   2. 便携模式：exe 同目录存在 portable.ini 时启用，
 *      配置与数据全部落在 exe 同目录的 data\ 下，可直接放 U 盘带走，不留注册表。
 *
 * 判定发生在 main() 最早期，之后所有模块统一通过本类取路径，
 * 避免各处硬编码 QStandardPaths 导致两套模式行为不一致。
 */
class AppPaths {
public:
    enum class Mode { Installed, Portable };

    /**
     * 必须在创建 QApplication 之前调用。
     * @param appDir exe 所在目录（QCoreApplication::applicationDirPath()）
     * @param forcePortable 命令行 --portable 指定的强制便携
     */
    static void initialize(const QString& appDir, bool forcePortable = false);

    static Mode mode();
    static bool portable();
    /** 人类可读的模式说明，用于「关于」对话框。 */
    static QString modeText();

    /** exe 所在目录。 */
    static QString appDir();
    /** 可写数据根目录。 */
    static QString dataDir();
    static QString logDir();
    static QString vlcCacheDir();
    static QString thumbCacheDir();
    /** 配置文件路径（便携模式下为 ini，安装模式下此项仅用于展示）。 */
    static QString configFile();
    /** 便携模式开关文件路径（exe 同目录 portable.ini）。 */
    static QString portableFlagFile();

    /**
     * 让 QSettings 使用与当前模式匹配的存储。
     * 必须在 QCoreApplication::setOrganizationName / setApplicationName 之后调用。
     */
    static void applySettingsFormat();

    /** 切换便携模式。需要在下一次启动时生效（返回后提示用户重启）。 */
    static bool setPortable(bool enabled, QString* err = nullptr);

private:
    static AppPaths& inst();
    Mode m_mode = Mode::Installed;
    QString m_appDir;
    QString m_dataDir;
};
