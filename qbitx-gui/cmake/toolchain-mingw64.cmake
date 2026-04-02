# MinGW-w64 cross-compilation toolchain for qbitx-gui (Ubuntu/WSL host -> Windows target).
# Prevents host Linux headers/libs from leaking into the build (no -isystem /usr/include).
#
# Usage:
#   cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#     -DQt6_DIR=$HOME/Qt/6.6.3/mingw_64/lib/cmake/Qt6 -DQT_HOST_PATH=$HOME/Qt/6.6.3/gcc_64 \
#     -DQBITX_GUI_DISABLE_VULKAN=ON

set(CMAKE_SYSTEM_NAME Windows)

# MinGW-w64 compilers (Debian/Ubuntu: install mingw-w64)
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER  x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER   x86_64-w64-mingw32-windres)

# Sysroot: target headers/libs only (no host /usr)
set(MINGW_SYSROOT "/usr/x86_64-w64-mingw32")
if(EXISTS "${MINGW_SYSROOT}")
    set(CMAKE_SYSROOT "${MINGW_SYSROOT}")
endif()

# Search only in sysroot (and Qt find root) for libs/includes/packages; never use host /usr
set(CMAKE_FIND_ROOT_PATH "${MINGW_SYSROOT}")
# Programs (moc, rcc, uic): use host — pass QT_HOST_PATH when configuring
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
