#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Remove ETIL build artifacts.
#
# Usage: clean.sh [debug|release|all]
#   debug       Remove build-debug/
#   release     Remove build/
#   all         Remove both (default)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

# --- Parse args ---
TARGET="${1:-all}"

case "$TARGET" in
    debug|release|all) ;;
    --help|-h)
        echo "Usage: $0 [debug|release|all]"
        echo ""
        echo "  debug       Remove build-debug/"
        echo "  release     Remove build/"
        echo "  all         Remove both (default)"
        exit 0
        ;;
    *)
        echo "Unknown argument: $TARGET"
        echo "Run with --help for usage."
        exit 1
        ;;
esac

# --- Clean function ---
clean_dir() {
    local label="$1"
    local dir="$2"

    if [[ ! -d "$dir" ]]; then
        etil_log "$label: $dir does not exist, nothing to clean"
        return
    fi

    local size
    size=$(du -sh "$dir" 2>/dev/null | cut -f1)
    etil_log "$label: removing $dir ($size)"
    rm -rf "$dir"
    etil_log "$label: done"
}

# --- Execute ---
etil_log "ETIL v$ETIL_VERSION — clean target: $TARGET"

case "$TARGET" in
    debug)
        clean_dir "debug" "$ETIL_BUILD_DEBUG_DIR"
        ;;
    release)
        clean_dir "release" "$ETIL_BUILD_RELEASE_DIR"
        ;;
    all)
        clean_dir "debug" "$ETIL_BUILD_DEBUG_DIR"
        clean_dir "release" "$ETIL_BUILD_RELEASE_DIR"
        ;;
esac

etil_log "Clean done."
