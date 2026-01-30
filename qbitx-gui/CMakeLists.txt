cmake_minimum_required(VERSION 3.16)
project(qbitx-gui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Widgets)
qt_add_executable(qbitx-gui
    main.cpp
    settingsmanager.h
    settingsmanager.cpp
    clibridge.h
    clibridge.cpp
    walletmanager.h
    walletmanager.cpp
    pathutil.h
    pathutil.cpp
    qml/main.qml
    qml/pages/Dashboard.qml
    qml/pages/Wallets.qml
    qml/pages/Addresses.qml
    qml/pages/Balance.qml
    qml/pages/Send.qml
    qml/pages/History.qml
    qml/pages/Settings.qml
)

qt_add_qml_module(qbitx-gui
    URI QBitX
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/pages/Dashboard.qml
        qml/pages/Wallets.qml
        qml/pages/Addresses.qml
        qml/pages/Balance.qml
        qml/pages/Send.qml
        qml/pages/History.qml
        qml/pages/Settings.qml
)

set_source_files_properties(qml/main.qml PROPERTIES QT_RESOURCE_ALIAS main.qml)

target_link_libraries(qbitx-gui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::Widgets
)
