# Building qbitx-gui for Windows (MinGW cross-compile on Linux/WSL)

Build a Windows `.exe` from Ubuntu or WSL using MinGW-w64 and Qt 6 (e.g. 6.6.3).

## Prerequisites

- **MinGW-w64** (Ubuntu/Debian): `sudo apt install mingw-w64 ninja-build cmake`
- **Qt 6.6.3** for MinGW and for host (e.g. install with [aqt](https://github.com/miurahr/aqtinstall)):
  - Target: `$HOME/Qt/6.6.3/mingw_64`
  - Host (for moc/rcc/uic): `$HOME/Qt/6.6.3/gcc_64`

## One-command build

From the project root:

```bash
./scripts/build-win.sh
```

Output: `build-win/qbitx-gui.exe` (PE32+ Windows executable).

## Manual configure + build

```bash
cmake -S . -B build-win -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DQt6_DIR=$HOME/Qt/6.6.3/mingw_64/lib/cmake/Qt6 \
  -DQT_HOST_PATH=$HOME/Qt/6.6.3/gcc_64 \
  -DQBITX_GUI_DISABLE_VULKAN=ON
cmake --build build-win -j$(nproc)
```

## Validation

- No host headers in compile commands:
  `ninja -C build-win -t commands | grep -F "/usr/include"` → should print nothing.
- Windows binary:
  `file build-win/qbitx-gui.exe` → `PE32+ executable (...) Windows`

## Custom Qt paths

Set before running the script or cmake:

- `QT_MINGW` / `Qt6_DIR`: Qt for MinGW (target).
- `QT_HOST` / `QT_HOST_PATH`: Qt for host (moc, rcc, uic).
