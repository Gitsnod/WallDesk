#include "SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>

#include <windows.h>

namespace {
/** 把实例名限定到当前用户会话，避免多用户 / 提权场景下相互干扰。 */
QString scopedName(const QString& key)
{
    wchar_t buffer[64] = {0};
    DWORD size = DWORD(sizeof(buffer) / sizeof(buffer[0]));
    if (GetUserNameW(buffer, &size)) {
        return key + QStringLiteral("_") + QString::fromWCharArray(buffer);
    }
    return key;
}
}

SingleInstance::SingleInstance(const QString& key, QObject* parent)
    : QObject(parent)
    , m_serverName(scopedName(key))
{
}

SingleInstance::~SingleInstance()
{
    if (m_server) {
        m_server->close();
    }
}

bool SingleInstance::tryRun(const QString& command)
{
    // 先试着连一次：能连上说明已经有实例在跑
    QLocalSocket probe;
    probe.connectToServer(m_serverName);
    if (probe.waitForConnected(300)) {
        const QByteArray payload = command.isEmpty() ? QByteArray("show\n")
                                                     : QByteArray("--") + command.toUtf8() + "\n";
        probe.write(payload);
        probe.waitForBytesWritten(500);
        probe.disconnectFromServer();
        return false;
    }

    // 监听失败（比如上次崩溃留下残留）时先清理再重试一次
    m_server = new QLocalServer(this);
    if (!m_server->listen(m_serverName)) {
        QLocalServer::removeServer(m_serverName);
        if (!m_server->listen(m_serverName)) {
            // 仍然失败：退化为允许多实例，不能因此不让用户启动程序
            return true;
        }
    }
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
    return true;
}

void SingleInstance::onNewConnection()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        socket->waitForReadyRead(500);
        const QByteArray payload = socket->readAll();
        socket->deleteLater();

        const QString line = QString::fromUtf8(payload).trimmed();
        if (line.isEmpty() || line.compare(QStringLiteral("show"), Qt::CaseInsensitive) == 0) {
            emit activateRequested();
            continue;
        }
        emit commandReceived(line.startsWith(QStringLiteral("--")) ? line.mid(2) : line);
    }
}
