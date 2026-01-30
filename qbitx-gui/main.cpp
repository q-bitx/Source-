#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include "settingsmanager.h"
#include "clibridge.h"
#include "walletmanager.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("qbitx-gui");
    app.setOrganizationName("qbitx");

    // Register C++ types
    qmlRegisterType<SettingsManager>("QBitX", 1, 0, "SettingsManager");
    qmlRegisterType<CliBridge>("QBitX", 1, 0, "CliBridge");
    qmlRegisterType<WalletManager>("QBitX", 1, 0, "WalletManager");

    QQmlApplicationEngine engine;
    
    // Create singleton instances
    SettingsManager* settingsManager = new SettingsManager(&app);
    CliBridge* cliBridge = new CliBridge(settingsManager, &app);
    WalletManager* walletManager = new WalletManager(settingsManager, &app);
    
    // Auto-detect qbitx-cli path if not configured
    settingsManager->autoDetectCliPath();
    
    // Make them available to QML
    engine.rootContext()->setContextProperty("settingsManager", settingsManager);
    engine.rootContext()->setContextProperty("cliBridge", cliBridge);
    engine.rootContext()->setContextProperty("walletManager", walletManager);
    
    const QUrl url(QStringLiteral("qrc:/QBitX/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    
    engine.load(QUrl(QStringLiteral("qrc:/QBitX/qml/main.qml")));
    
    return app.exec();
}
