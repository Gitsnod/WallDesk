#pragma once

#include <QString>
#include <QtGlobal>

/**
 * 运行日志（V4 新增，面向分发后的远程排障）。
 *
 * 分发给他人后，对方遇到问题时最有用的就是一份日志：
 *   - 安装模式：%LOCALAPPDATA%\WallDesk\logs\WallDesk.log
 *   - 便携模式：<程序目录>\data\logs\WallDesk.log
 *
 * 安装 qInstallMessageHandler 后，Qt 自身的警告 / 崩溃断言，以及本程序调用
 * Logger::info / warning 的内容都会落盘；单文件超过 2 MB 自动滚动，最多保留 3 份。
 * 默认记录 Warning 及以上，避免刷爆磁盘；命令行 --verbose 可开到 Debug。
 */
class Logger {
public:
    /** 安装消息处理器并写入启动横幅（版本、Qt 版本、运行模式）。 */
    static void install(bool verbose = false);

    static QString logDir();
    static QString logFilePath();

    /** 主动记录一条信息级日志。不受 --verbose 影响：调用点都是关键事件。 */
    static void info(const QString& message);
    static void warning(const QString& message);
    static void critical(const QString& message);

    /** 实际写入。消息处理器与本类的便捷接口共用同一入口，保证加锁与滚动一致。 */
    static void write(QtMsgType type, const QString& message);
};
