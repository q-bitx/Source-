#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QDir>
#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWindow>
#include <QTimer>
#include <QProcess>
#include <QFile>
#include <QtCore/qlogging.h>
#include <QtCore/qglobal.h>
#include "settingsmanager.h"
#include "clibridge.h"
#include "walletmanager.h"
#include "clirunner.h"
#include "logmanager.h"
#include "nodemanager.h"
#include "rpcwalletbootstrap.h"
#include "traymanager.h"
#include "appinstance.h"

static bool debugInputEnabled()
{
    const QByteArray v = qgetenv("QBITX_GUI_DEBUG_INPUT");
    return !v.isEmpty() && v != "0";
}

// Non-consuming global input logger: enabled only when QBITX_GUI_DEBUG_INPUT is set.
class InputSpy : public QObject
{
public:
    explicit InputSpy(QObject *parent = nullptr) : QObject(parent) {}
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        const char *target = obj ? obj->metaObject()->className() : "null";
        switch (ev->type()) {
        case QEvent::MouseButtonPress: {
            auto *e = static_cast<QMouseEvent *>(ev);
            const char *btn = (e->button() == Qt::LeftButton) ? "Left" : (e->button() == Qt::RightButton) ? "Right" : "Other";
            qInfo("[INPUT] type=MousePress button=%s pos=(%d,%d) target=%s", btn, (int)e->position().x(), (int)e->position().y(), target);
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto *e = static_cast<QMouseEvent *>(ev);
            const char *btn = (e->button() == Qt::LeftButton) ? "Left" : (e->button() == Qt::RightButton) ? "Right" : "Other";
            qInfo("[INPUT] type=MouseRelease button=%s pos=(%d,%d) target=%s", btn, (int)e->position().x(), (int)e->position().y(), target);
            break;
        }
        case QEvent::MouseMove: {
            auto *e = static_cast<QMouseEvent *>(ev);
            qInfo("[INPUT] type=MouseMove pos=(%d,%d) target=%s", (int)e->position().x(), (int)e->position().y(), target);
            break;
        }
        case QEvent::Wheel: {
            auto *e = static_cast<QWheelEvent *>(ev);
            qInfo("[INPUT] type=Wheel pos=(%d,%d) target=%s", (int)e->position().x(), (int)e->position().y(), target);
            break;
        }
        case QEvent::KeyPress: {
            auto *e = static_cast<QKeyEvent *>(ev);
            qInfo("[INPUT] type=KeyPress key=0x%x target=%s", e->key(), target);
            break;
        }
        case QEvent::KeyRelease: {
            auto *e = static_cast<QKeyEvent *>(ev);
            qInfo("[INPUT] type=KeyRelease key=0x%x target=%s", e->key(), target);
            break;
        }
        default:
            break;
        }
        return false; // never consume
    }
};

// Global event filter for input diagnosis: log only with qInfo(), do not touch LogManager or QML.
class GlobalEventFilter : public QObject
{
public:
    explicit GlobalEventFilter(QObject *parent = nullptr) : QObject(parent) {}
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *e = static_cast<QMouseEvent *>(event);
            qInfo("INPUT [C++] MouseButtonPress at (%d,%d) button=0x%x", (int)e->position().x(), (int)e->position().y(), (int)e->button());
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto *e = static_cast<QMouseEvent *>(event);
            qInfo("INPUT [C++] MouseButtonRelease at (%d,%d) button=0x%x", (int)e->position().x(), (int)e->position().y(), (int)e->button());
            break;
        }
        case QEvent::MouseMove: {
            auto *e = static_cast<QMouseEvent *>(event);
            qInfo("INPUT [C++] MouseMove at (%d,%d)", (int)e->position().x(), (int)e->position().y());
            break;
        }
        case QEvent::Wheel: {
            auto *e = static_cast<QWheelEvent *>(event);
            qInfo("INPUT [C++] Wheel at (%d,%d)", (int)e->position().x(), (int)e->position().y());
            break;
        }
        case QEvent::TouchBegin:
            qInfo("INPUT [C++] TouchBegin");
            break;
        case QEvent::KeyPress: {
            auto *e = static_cast<QKeyEvent *>(event);
            qInfo("INPUT [C++] KeyPress key=0x%x", e->key());
            break;
        }
        default:
            break;
        }
        return false; // never consume
    }
};

static void logMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Must never call qDebug/qWarning/qInfo here (infinite recursion -> segfault).
    const char *level = "DEBUG";
    switch (type) {
    case QtDebugMsg:   level = "DEBUG"; break;
    case QtInfoMsg:    level = "INFO"; break;
    case QtWarningMsg: level = "WARNING"; break;
    case QtCriticalMsg: level = "CRITICAL"; break;
    case QtFatalMsg:   level = "FATAL"; break;
    }
    QString full = msg;
    if (context.file && (type >= QtWarningMsg))
        full = QString("%1 (%2:%3 %4)").arg(msg).arg(context.file).arg(context.line).arg(context.function ? context.function : "");
    if (LogManager *lm = LogManager::instance())
        lm->appendFromMessageHandler(level, full);
}

static bool isWsl()
{
    if (!qEnvironmentVariableIsEmpty("WSL_DISTRO_NAME"))
        return true;
    QFile f(QStringLiteral("/proc/version"));
    if (f.open(QIODevice::ReadOnly))
        return f.readAll().contains("Microsoft");
    return false;
}

int main(int argc, char *argv[])
{
    // WSL: set DISPLAY to default gateway if empty or broken (e.g. "10.0.0.1" from WSL2).
    if (isWsl()) {
        QByteArray disp = qgetenv("DISPLAY");
        if (disp.isEmpty() || disp.startsWith("10.")) {
            QProcess p;
            p.start(QStringLiteral("sh"), { QStringLiteral("-c"), QStringLiteral("ip route | awk '/default/ {print $3}'") }, QProcess::ReadOnly);
            if (p.waitForFinished(2000)) {
                QByteArray gw = p.readAllStandardOutput().trimmed();
                if (!gw.isEmpty()) {
                    qputenv("DISPLAY", gw + ":0.0");
                    qInfo() << "WSL detected, DISPLAY set to" << qgetenv("DISPLAY");
                }
            }
        }
    }

    // Safe graphics fallback on Linux/WSL: use software backend when EGL/DRI fails and user did not override.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND") && qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", "software");
    }

    QApplication app(argc, argv);
    app.setApplicationName("qbitx-gui");
    app.setOrganizationName("qbitx");
    app.setApplicationDisplayName(QStringLiteral("Q-BitX Wallet"));
    {
        QIcon appIcon(QStringLiteral(":/QBitX/assets/qbitx_wallet_icon.ico"));
        if (!appIcon.isNull())
            app.setWindowIcon(appIcon);
    }
    QApplication::setQuitOnLastWindowClosed(false);

    AppInstance appInstance;
    if (appInstance.tryActivateExistingInstance())
        return 0;

    if (debugInputEnabled()) {
        InputSpy *inputSpy = new InputSpy(&app);
        app.installEventFilter(inputSpy);
    }

#ifdef ENABLE_EVENT_FILTER_LOGGING
    // Extra diagnosis when explicitly enabled. Never consumes events.
    GlobalEventFilter *globalFilter = new GlobalEventFilter(&app);
    app.installEventFilter(globalFilter);
    qRegisterMetaType<QWindow *>("QWindow*");
    QObject::connect(&app, &QGuiApplication::focusWindowChanged, &app, [](QWindow *w) {
        qInfo("INPUT [C++] focusWindowChanged: %p %s", (void *)w, w ? qPrintable(w->title()) : "null");
    });
#endif

#ifdef ENABLE_EVENT_FILTER_LOGGING
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &app, [](Qt::ApplicationState state) {
        const char *s = (state == Qt::ApplicationActive) ? "Active" : (state == Qt::ApplicationInactive) ? "Inactive" : (state == Qt::ApplicationSuspended) ? "Suspended" : "Hidden";
        qInfo("[INPUT] applicationStateChanged: %s", s);
    });
#endif

    QDir::setCurrent(QCoreApplication::applicationDirPath());

    LogManager *logManager = new LogManager(&app);
    LogManager::setInstance(logManager);
    qInstallMessageHandler(logMessageHandler);
    SettingsManager *settingsManager = new SettingsManager(&app);

    NodeManager *nodeManager = new NodeManager(&app);
    QObject::connect(nodeManager, &NodeManager::gracefulShutdownFinished, &app, [logManager]() {
        if (logManager)
            logManager->flushAll();
        QCoreApplication::quit();
    });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, logManager, &LogManager::flushAll);

    TrayManager *trayManager = new TrayManager(&app);
    if (!nodeManager->startNode()) {
        qWarning() << "Embedded QBitX node did not start; CLI features may fail until qbitx is available.";
    }

    settingsManager->setDatadir(nodeManager->dataDir());

    qmlRegisterType<SettingsManager>("QBitX", 1, 0, "SettingsManager");
    qmlRegisterType<CliBridge>("QBitX", 1, 0, "CliBridge");
    qmlRegisterType<WalletManager>("QBitX", 1, 0, "WalletManager");

    QQmlApplicationEngine engine;

    CliBridge *cliBridge = new CliBridge(settingsManager, &app);
    WalletManager *walletManager = new WalletManager(settingsManager, cliBridge, &app);
    CliRunner *cliRunner = new CliRunner(&app);

    cliBridge->setLogManager(logManager);
    cliRunner->setLogManager(logManager);

    if (settingsManager->useAutoDetectCli())
        settingsManager->autoDetectCliPath();

    auto *rpcBootstrap = new RpcWalletBootstrap(settingsManager, logManager, &app);
    QObject::connect(rpcBootstrap, &RpcWalletBootstrap::walletLoadPhaseCompleted, walletManager,
                     &WalletManager::refreshWallets);
    QObject::connect(rpcBootstrap, &RpcWalletBootstrap::rpcReadinessTimedOut, &app,
                     [](const QString &msg) { qWarning() << "RpcWalletBootstrap:" << msg; });
    if (nodeManager->isRunning())
        rpcBootstrap->begin();

    // Context objects must outlive the engine: parent to app so they are never deleted during refresh/navigation.
    engine.rootContext()->setContextProperty("settingsManager", settingsManager);
    engine.rootContext()->setContextProperty("cliBridge", cliBridge);
    engine.rootContext()->setContextProperty("walletManager", walletManager);
    engine.rootContext()->setContextProperty("cliRunner", cliRunner);
    engine.rootContext()->setContextProperty("logManager", logManager);
    engine.rootContext()->setContextProperty("rpcBootstrap", rpcBootstrap);
    engine.rootContext()->setContextProperty("nodeManager", nodeManager);
    engine.rootContext()->setContextProperty("trayManager", trayManager);

    const QUrl url(QStringLiteral("qrc:/QBitX/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "QML root window was not created";
        if (nodeManager && nodeManager->isRunning())
            nodeManager->requestGracefulShutdown();
        if (logManager)
            logManager->flushAll();
        return -1;
    }

    // Force root window to take focus so clicks work (fix for WSL/xcb/xwayland).
    QObject *root = engine.rootObjects().first();
    QWindow *win = qobject_cast<QWindow *>(root);
    if (win) {
        trayManager->setMainWindow(win);
        appInstance.setMainWindow(win);
        win->raise();
        win->requestActivate();
        QTimer::singleShot(100, &app, [win]() { win->requestActivate(); });
    }

    return app.exec();
}
