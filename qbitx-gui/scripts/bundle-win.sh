#!/usr/bin/env bash
# Deploy Windows bundle and strip runtime / datadir artifacts from the install tree.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build-win}"
BUNDLE_DIR="${BUNDLE_DIR:-$PROJECT_ROOT/dist/bundle}"
QT_MINGW="${QT_MINGW:-$HOME/Qt/6.6.3/mingw_64}"
WINDEPLOYQT="${WINDEPLOYQT:-$QT_MINGW/bin/windeployqt.exe}"

EXE="$BUILD_DIR/qbitx-gui.exe"
if [[ ! -f "$EXE" ]]; then
  echo "Missing $EXE — build first (scripts/build-win.sh)" >&2
  exit 1
fi

rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR"
cp "$EXE" "$BUNDLE_DIR/"

# Copy embedded node binaries if present next to build output
for bin in qbitx qbitx-cli qbitx-tx; do
  if [[ -f "$BUILD_DIR/$bin.exe" ]]; then
    cp "$BUILD_DIR/$bin.exe" "$BUNDLE_DIR/"
  elif [[ -f "$PROJECT_ROOT/../bin/$bin.exe" ]]; then
    cp "$PROJECT_ROOT/../bin/$bin.exe" "$BUNDLE_DIR/"
  fi
done

echo "Running windeployqt…"
"$WINDEPLOYQT" --qmldir "$PROJECT_ROOT/qml" --no-translations "$BUNDLE_DIR/qbitx-gui.exe"

# Never ship local runtime / wallet / chain data in the installer bundle
RUNTIME_PATTERNS=(
  'wallet.dat'
  '*.dat'
  '*.bak'
  '*.backup'
  'debug.log'
  'qbitx.conf'
  'peers.dat'
  'banlist.json'
  'mempool.dat'
  'pqwallet*'
  'wallet_backup*'
)
RUNTIME_DIRS=(blocks chainstate wallets)

for pat in "${RUNTIME_PATTERNS[@]}"; do
  find "$BUNDLE_DIR" -name "$pat" -print -delete 2>/dev/null || true
done
for dir in "${RUNTIME_DIRS[@]}"; do
  find "$BUNDLE_DIR" -type d -name "$dir" -print -exec rm -rf {} + 2>/dev/null || true
done

echo "Bundle ready: $BUNDLE_DIR"
find "$BUNDLE_DIR" -maxdepth 1 -type f | sort
