#!/usr/bin/env bash
# Build all ETIL dependencies for CI.
# Builds both debug and release into the prefix directory.
#
# Usage:
#   ./ci/deps/build-deps.sh [PREFIX]
#
# Default PREFIX: /opt/etil-deps/v1
# Run inside the CI container for ABI compatibility.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${1:-/opt/etil-deps/v1}"
MODE_ARG="${2:-all}"  # debug|release|all

echo "=== Building ETIL dependencies ==="
echo "Prefix: $PREFIX"
echo "Mode:   $MODE_ARG"
echo "Script: $SCRIPT_DIR/CMakeLists.txt"
echo ""

# Determine which modes to build
case "$MODE_ARG" in
    debug)   MODES="debug" ;;
    release) MODES="release" ;;
    all)     MODES="debug release" ;;
    *)
        echo "Error: invalid mode '$MODE_ARG' (expected: debug|release|all)"
        exit 1
        ;;
esac

for MODE in $MODES; do
    INSTALL_DIR="$PREFIX/$MODE"
    BUILD_DIR="/tmp/etil-deps-build-$MODE"

    if [ "$MODE" = "debug" ]; then
        BUILD_TYPE="Debug"
    else
        BUILD_TYPE="Release"
    fi

    echo "--- Building $MODE ($BUILD_TYPE) ---"
    echo "Install: $INSTALL_DIR"
    echo "Build:   $BUILD_DIR"

    mkdir -p "$INSTALL_DIR"
    rm -rf "$BUILD_DIR"

    cmake -GNinja \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DDEPS_INSTALL_PREFIX="$INSTALL_DIR" \
        -S "$SCRIPT_DIR" \
        -B "$BUILD_DIR"

    ninja -j4 -C "$BUILD_DIR"

    echo "--- $MODE complete ---"
    echo ""
done

# Copy manifest for version tracking
cp "$SCRIPT_DIR/manifest.json" "$PREFIX/manifest.json"

echo "=== All dependencies built ==="
echo "Debug:   $PREFIX/debug"
echo "Release: $PREFIX/release"
echo "Manifest: $PREFIX/manifest.json"
