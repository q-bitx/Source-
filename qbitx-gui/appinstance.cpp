#include "appinstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QWindow>

namespace {
constexpr auto kServerName = "qbitx-gui-single-instance-v1";
}

AppInstance::AppInstance(QObject *parent)
    : QObject(parent)
    , m_server(nullptr)
    , m_window(nullptr)
{
}

AppInstance::~AppInstance()
{
    if (m_server) {
        m_server->close();
        QLocalServer::removeServer(QString::fromLatin1(kServerName));
    }
}

bool AppInstance::tryActivateExistingInstance()
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kServerName));
    if (!socket.waitForConnected(300))
        return false;

    socket.write("activate");
    socket.flush();
    socket.waitForBytesWritten(300);
    return true;
}

void AppInstance::setMainWindow(QWindow *window)
{
    m_window = window;
    if (!m_server) {
        m_server = new QLocalServer(this);
        connect(m_server, &QLocalServer::newConnection, this, &AppInstance::onNewConnection);
        QLocalServer::removeServer(QString::fromLatin1(kServerName));
        if (!m_server->listen(QString::fromLatin1(kServerName)))
            m_server = nullptr;
    }
}

void AppInstance::onNewConnection()
{
    if (!m_server)
        return;
    QLocalSocket *client = m_server->nextPendingConnection();
    if (!client)
        return;
    connect(client, &QLocalSocket::readyRead, this, [this, client]() {
        const QByteArray msg = client->readAll();
        if (msg.contains("activate") && m_window) {
            m_window->show();
            m_window->raise();
            m_window->requestActivate();
        }
        client->deleteLater();
    });
    connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
}
