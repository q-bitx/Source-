#ifndef TRAYMANAGER_H
#define TRAYMANAGER_H

#include <QObject>

class QMenu;
class QSystemTrayIcon;
class QWindow;

class TrayManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ isAvailable NOTIFY availableChanged)

public:
    explicit TrayManager(QObject *parent = nullptr);
    ~TrayManager() override;

    bool isAvailable() const { return m_available; }
    void setMainWindow(QWindow *window);

    Q_INVOKABLE void showTrayIcon();
    Q_INVOKABLE void hideTrayIcon();
    Q_INVOKABLE void hideToTray();
    Q_INVOKABLE void restoreFromTray();
    Q_INVOKABLE void showBackgroundNotification();

signals:
    void availableChanged();
    void openRequested();
    void exitRequested();

private:
    void rebuildMenu();
    static QIcon loadTrayIcon();

    QWindow *m_window;
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    bool m_available;
};

#endif // TRAYMANAGER_H
