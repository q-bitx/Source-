#ifndef APPINSTANCE_H
#define APPINSTANCE_H

#include <QObject>

class QLocalServer;
class QWindow;

class AppInstance : public QObject
{
    Q_OBJECT

public:
    explicit AppInstance(QObject *parent = nullptr);
    ~AppInstance() override;

    bool tryActivateExistingInstance();
    void setMainWindow(QWindow *window);

private:
    void onNewConnection();

    QLocalServer *m_server;
    QWindow *m_window;
};

#endif // APPINSTANCE_H
