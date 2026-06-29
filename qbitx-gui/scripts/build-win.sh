#!/usr/bin/env bash
# Cross-build qbitx-gui for Windows (MinGW) on Ubuntu/WSL.
# Requires: mingw-w64, Ninja, Qt 6.6.3 mingw_64 + gcc_64 (e.g. via aqt).
#
# Qt paths (override with env if needed):
#   Qt6_DIR / QT_HOST_PATH default to $HOME/Qt/6.6.3/mingw_64 and gcc_64.

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build-win}"
QT_MINGW="${QT_MINGW:-$HOME/Qt/6.6.3/mingw_64}"
QT_HOST="${QT_HOST:-$HOME/Qt/6.6.3/gcc_64}"

echo "Configuring MinGW build in $BUILD_DIR"
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/toolchain-mingw64.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DQt6_DIR="$QT_MINGW/lib/cmake/Qt6" \
  -DQT_HOST_PATH="$QT_HOST" \
  -DQBITX_GUI_DISABLE_VULKAN=ON

echo "Building..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "Checking for host header leakage (should be empty):"
if ninja -C "$BUILD_DIR" -t commands 2>/dev/null | grep -F "/usr/include" || true; then
  echo "WARNING: Some commands still reference /usr/include"
else
  echo "OK: no /usr/include in compile commands"
fi

echo "Output: $BUILD_DIR/qbitx-gui.exe"
file "$BUILD_DIR/qbitx-gui.exe" || true
