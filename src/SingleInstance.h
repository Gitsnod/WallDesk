#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

/**
 * 单实例守卫（V4 新增）。
 *
 * 分发给他人后，最常见的误操作是双击好几次图标、或开机自启后又手动启动一次，
 * 结果多个实例同时改注册表、抢桌面层，表现是壁纸来回跳。
 *
 * 实现：本地套接字（QLocalServer）。首个实例监听；后续实例连接成功后
 * 通过 activateRequested / commandReceived 把指令交给已有实例，然后自行退出。
 * 用 QLocalServer 而非 Win32 互斥量的原因：它顺带解决了「把命令行传给已有实例」
 * 这个需求（开机自启 + 手动启动传 --next 等），且实现跨平台。
 */
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString& key = QStringLiteral("WallDesk"),
                            QObject* parent = nullptr);
    ~SingleInstance() override;

    /**
     * 尝试成为唯一实例。
     * @param command 已有实例存在时发送给它的指令（如 "--next"）
     * @return true 表示本实例是第一个，应继续启动；false 表示已有实例，应立即退出
     */
    bool tryRun(const QString& command = QString());

signals:
    /** 其它实例请求显示主界面。 */
    void activateRequested();
    /** 其它实例发来的指令（已去除 -- 前缀，如 "next"）。 */
    void commandReceived(const QString& command);

private slots:
    void onNewConnection();

private:
    QString m_serverName;
    QLocalServer* m_server = nullptr;
};
