#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QObject>

#include "AppPaths.h"
#include "Logger.h"
#include "MainWindow.h"
#include "SingleInstance.h"

int main(int argc, char* argv[])
{
    // 缩放自适应：Qt6 默认把缩放系数取整，这里放开为原值，125%/150% 下不再模糊或跳变
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("WallDesk"));
    QCoreApplication::setApplicationName(QStringLiteral("WallDesk"));
    QCoreApplication::setApplicationVersion(QStringLiteral(WALLDESK_VERSION));
    QApplication::setQuitOnLastWindowClosed(false); // 关窗后仍靠托盘常驻

    // ---- 命令行 ----
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("WallDesk — Windows 图片 / 视频壁纸工具"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption minimizedOpt(QStringLiteral("minimized"),
                                          QStringLiteral("启动后仅驻留托盘，不显示主界面"));
    const QCommandLineOption nextOpt(QStringLiteral("next"),
                                     QStringLiteral("通知已运行的实例切换到下一张壁纸"));
    const QCommandLineOption quitOpt(QStringLiteral("quit"),
                                     QStringLiteral("通知已运行的实例退出"));
    const QCommandLineOption portableOpt(QStringLiteral("portable"),
                                         QStringLiteral("本次启动强制使用便携模式"));
    const QCommandLineOption verboseOpt(QStringLiteral("verbose"),
                                        QStringLiteral("在日志中额外记录调试信息"));
    parser.addOption(minimizedOpt);
    parser.addOption(nextOpt);
    parser.addOption(quitOpt);
    parser.addOption(portableOpt);
    parser.addOption(verboseOpt);
    parser.process(app);

    // 路径与日志必须先于一切可写操作初始化（便携模式探测决定配置/数据落点）
    AppPaths::initialize(QCoreApplication::applicationDirPath(), parser.isSet(portableOpt));
    AppPaths::applySettingsFormat();
    Logger::install(parser.isSet(verboseOpt));

    // 单实例：后启动的实例把指令转交给已有实例后立刻退出，
    // 避免两份实例同时写注册表、抢占桌面图层。
    SingleInstance guard;
    const QString forward = parser.isSet(quitOpt) ? QStringLiteral("quit")
                          : parser.isSet(nextOpt) ? QStringLiteral("next")
                                                  : QString();
    if (!guard.tryRun(forward)) {
        Logger::info(QStringLiteral("已有实例在运行，转交指令后退出：%1")
                         .arg(forward.isEmpty() ? QStringLiteral("show") : forward));
        return 0;
    }
    Logger::info(QStringLiteral("单实例检查通过，作为主实例运行"));

    MainWindow w;
    Logger::info(QStringLiteral("主窗口构造完成"));
    QObject::connect(&guard, &SingleInstance::activateRequested, &w, &MainWindow::requestShow);
    QObject::connect(&guard, &SingleInstance::commandReceived, &w, &MainWindow::handleCommand);
    if (!parser.isSet(minimizedOpt)) {
        w.show();
    }

    const int rc = app.exec();
    Logger::info(QStringLiteral("事件循环退出，返回值=%1").arg(rc));
    return rc;
}
