#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Build ETIL project (debug, release, or both).
#
# Usage: build.sh [debug|release|all] [--configure] [--clean]
#   debug       Build debug only (default)
#   release     Build release only
#   all         Build both
#   --configure Force CMake reconfigure
#   --clean     Clean before building (rm -rf build dir, then configure + build)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

etil_require_cmd cmake
etil_require_cmd ninja

# --- Parse args ---
TARGET="debug"
FORCE_CONFIGURE=false
CLEAN_FIRST=false

for arg in "$@"; do
    case "$arg" in
        debug|release|all) TARGET="$arg" ;;
        --configure)       FORCE_CONFIGURE=true ;;
        --clean)           CLEAN_FIRST=true ;;
        --help|-h)
            echo "Usage: $0 [debug|release|all] [--configure] [--clean]"
            echo ""
            echo "  debug       Build debug only (default)"
            echo "  release     Build release only"
            echo "  all         Build both"
            echo "  --configure Force CMake reconfigure"
            echo "  --clean     Clean before building (rm -rf build dir, then configure + build)"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Run with --help for usage."
            exit 1
            ;;
    esac
done

# --- Build function for a single target ---
build_target() {
    local build_type="$1"
    local build_dir

    if [[ "$build_type" == "Debug" ]]; then
        build_dir="$ETIL_BUILD_DEBUG_DIR"
    else
        build_dir="$ETIL_BUILD_RELEASE_DIR"
    fi

    etil_log "Building $build_type → $build_dir"

    # Clean if requested
    if [[ "$CLEAN_FIRST" == true && -d "$build_dir" ]]; then
        etil_log "Cleaning $build_dir"
        rm -rf "$build_dir"
    fi

    # Configure if needed
    if [[ ! -d "$build_dir" || "$FORCE_CONFIGURE" == true || "$CLEAN_FIRST" == true ]]; then
        etil_log "Configuring CMake ($build_type)"
        cmake -GNinja \
            -DCMAKE_BUILD_TYPE="$build_type" \
            $ETIL_CMAKE_COMMON_FLAGS \
            -S "$ETIL_PROJECT_DIR" \
            -B "$build_dir"
    fi

    # Build
    local start=$SECONDS
    ninja -C "$build_dir"
    local elapsed=$((SECONDS - start))
    etil_log "$build_type build completed in ${elapsed}s"
}

# --- Execute ---
etil_log "ETIL v$ETIL_VERSION — build target: $TARGET"

case "$TARGET" in
    debug)
        build_target "Debug"
        ;;
    release)
        build_target "Release"
        ;;
    all)
        build_target "Debug"
        build_target "Release"
        ;;
esac

etil_log "Build done."
