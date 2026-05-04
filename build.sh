#!/usr/bin/env bash
# Out-of-source build: keeps the project directory clean. The build
# tree lives in ../EcuParser-build relative to the project root, so
# nothing gets written inside src/ or the project root itself. The
# resulting binary lives in EcuParser-build/release/EcuParser since
# the .pro file routes per-configuration outputs to release/ or
# debug/ subdirectories.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../EcuParser-build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Pick whichever qmake is available - on most distros it's qmake6,
# but some systems still call it qmake.
QMAKE="qmake6"
command -v "$QMAKE" >/dev/null 2>&1 || QMAKE="qmake"

"$QMAKE" "$SCRIPT_DIR/EcuParser.pro" CONFIG+=release
make -j"$(nproc 2>/dev/null || echo 2)"

echo ""
echo "Build complete. Binary: $BUILD_DIR/release/EcuParser"
echo "Run with:           cd $SCRIPT_DIR && \"$BUILD_DIR/release/EcuParser\""
