#include "traymanager.h"

#include <QAction>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWindow>

TrayManager::TrayManager(QObject *parent)
    : QObject(parent)
    , m_window(nullptr)
    , m_tray(nullptr)
    , m_menu(nullptr)
    , m_available(QSystemTrayIcon::isSystemTrayAvailable())
{
    if (!m_available)
        return;

    m_tray = new QSystemTrayIcon(loadTrayIcon(), this);
    m_tray->setToolTip(QStringLiteral("Q-BitX Wallet"));
    rebuildMenu();

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            restoreFromTray();
    });
}

TrayManager::~TrayManager()
{
    hideTrayIcon();
}

void TrayManager::setMainWindow(QWindow *window)
{
    m_window = window;
}

QIcon TrayManager::loadTrayIcon()
{
    QIcon icon(QStringLiteral(":/QBitX/assets/qbitx_wallet_icon.ico"));
    if (!icon.isNull())
        return icon;
    icon.addFile(QStringLiteral(":/QBitX/assets/sidebar_logo.png"));
    return icon;
}

void TrayManager::rebuildMenu()
{
    if (!m_tray)
        return;

    if (m_menu)
        m_menu->deleteLater();

    m_menu = new QMenu();
    QAction *openAction = m_menu->addAction(QStringLiteral("Open QBitX Wallet"));
    m_menu->addSeparator();
    QAction *exitAction = m_menu->addAction(QStringLiteral("Stop node and exit"));

    connect(openAction, &QAction::triggered, this, [this]() {
        restoreFromTray();
        emit openRequested();
    });
    connect(exitAction, &QAction::triggered, this, [this]() {
        emit exitRequested();
    });

    m_tray->setContextMenu(m_menu);
}

void TrayManager::showTrayIcon()
{
    if (!m_available || !m_tray)
        return;
    if (!m_tray->icon().isNull())
        m_tray->show();
}

void TrayManager::hideTrayIcon()
{
    if (m_tray)
        m_tray->hide();
}

void TrayManager::hideToTray()
{
    if (m_window)
        m_window->hide();
    showTrayIcon();
    showBackgroundNotification();
}

void TrayManager::restoreFromTray()
{
    if (!m_window)
        return;
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

void TrayManager::showBackgroundNotification()
{
    if (!m_available || !m_tray)
        return;
    m_tray->showMessage(QStringLiteral("QBitX Wallet"),
                        QStringLiteral("QBitX Wallet is still running in the background."),
                        QSystemTrayIcon::Information,
                        4000);
}
