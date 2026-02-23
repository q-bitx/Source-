#include <QGuiApplication>
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

// Non-consuming global input logger: always return false. ON by default until we confirm events reach the app.
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

    QGuiApplication app(argc, argv);
    app.setApplicationName("qbitx-gui");
    app.setOrganizationName("qbitx");

    // Non-consuming input spy: always on until we confirm events reach the app.
    InputSpy *inputSpy = new InputSpy(&app);
    app.installEventFilter(inputSpy);

#ifdef ENABLE_EVENT_FILTER_LOGGING
    // Extra diagnosis when explicitly enabled. Never consumes events.
    GlobalEventFilter *globalFilter = new GlobalEventFilter(&app);
    app.installEventFilter(globalFilter);
    qRegisterMetaType<QWindow *>("QWindow*");
    QObject::connect(&app, &QGuiApplication::focusWindowChanged, &app, [](QWindow *w) {
        qInfo("INPUT [C++] focusWindowChanged: %p %s", (void *)w, w ? qPrintable(w->title()) : "null");
    });
#endif

    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &app, [](Qt::ApplicationState state) {
        const char *s = (state == Qt::ApplicationActive) ? "Active" : (state == Qt::ApplicationInactive) ? "Inactive" : (state == Qt::ApplicationSuspended) ? "Suspended" : "Hidden";
        qInfo("[INPUT] applicationStateChanged: %s", s);
    });

    QDir::setCurrent(QCoreApplication::applicationDirPath());

    LogManager *logManager = new LogManager(&app);
    LogManager::setInstance(logManager);
    qInstallMessageHandler(logMessageHandler);

    qmlRegisterType<SettingsManager>("QBitX", 1, 0, "SettingsManager");
    qmlRegisterType<CliBridge>("QBitX", 1, 0, "CliBridge");
    qmlRegisterType<WalletManager>("QBitX", 1, 0, "WalletManager");

    QQmlApplicationEngine engine;

    SettingsManager* settingsManager = new SettingsManager(&app);
    CliBridge* cliBridge = new CliBridge(settingsManager, &app);
    WalletManager* walletManager = new WalletManager(settingsManager, cliBridge, &app);
    CliRunner* cliRunner = new CliRunner(&app);

    cliBridge->setLogManager(logManager);
    cliRunner->setLogManager(logManager);

    if (settingsManager->useAutoDetectCli())
        settingsManager->autoDetectCliPath();

    // Context objects must outlive the engine: parent to app so they are never deleted during refresh/navigation.
    engine.rootContext()->setContextProperty("settingsManager", settingsManager);
    engine.rootContext()->setContextProperty("cliBridge", cliBridge);
    engine.rootContext()->setContextProperty("walletManager", walletManager);
    engine.rootContext()->setContextProperty("cliRunner", cliRunner);
    engine.rootContext()->setContextProperty("logManager", logManager);
    
    const QUrl url(QStringLiteral("qrc:/QBitX/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    
    engine.load(QUrl(QStringLiteral("qrc:/QBitX/qml/main.qml")));

    // Log root window state and force it to take focus so clicks work (fix for WSL/xcb/xwayland).
    if (!engine.rootObjects().isEmpty()) {
        QObject *root = engine.rootObjects().first();
        QWindow *win = qobject_cast<QWindow *>(root);
        if (win) {
            qInfo("[INPUT] root window: isVisible=%d isActive=%d isExposed=%d flags=0x%x",
                  win->isVisible(), win->isActive(), win->isExposed(), (unsigned int)win->flags());
            win->raise();
            win->requestActivate();
            QTimer::singleShot(100, &app, [win]() { win->requestActivate(); });
        } else {
            qInfo("[INPUT] root object is not a QWindow: %s", root ? root->metaObject()->className() : "null");
        }
    } else {
        qInfo("[INPUT] no root objects after load");
    }

    return app.exec();
}
