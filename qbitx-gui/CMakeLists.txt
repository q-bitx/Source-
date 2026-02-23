cmake_minimum_required(VERSION 3.16)
project(qbitx-gui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Optional: enable AddressSanitizer for local debug (e.g. cmake -DQBITX_ASAN=ON ..)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND QBITX_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

# MinGW cross-build: avoid host Vulkan/WrapVulkanHeaders (/usr/include) leaking into compile commands
option(QBITX_GUI_DISABLE_VULKAN "Disable Vulkan (recommended for MinGW cross-build to avoid host headers)" OFF)
if(QBITX_GUI_DISABLE_VULKAN OR CMAKE_CROSSCOMPILING)
    set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan ON CACHE BOOL "Disable Vulkan for this build" FORCE)
endif()

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
    clirunner.h
    clirunner.cpp
    logmanager.h
    logmanager.cpp
    qml.qrc
)

qt_add_qml_module(qbitx-gui
    URI QBitX
    VERSION 1.0
)

target_link_libraries(qbitx-gui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::Widgets
)
