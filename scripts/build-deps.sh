#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Build ETIL pre-built dependencies locally.
#
# Usage: build-deps.sh [debug|release|all] [--force]
#   debug       Build debug deps only
#   release     Build release deps only
#   all         Build both (default)
#   --force     Rebuild even if manifest is up to date

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

etil_require_cmd cmake
etil_require_cmd ninja

# --- Parse args ---
MODE="all"
FORCE=false

for arg in "$@"; do
    case "$arg" in
        debug|release|all) MODE="$arg" ;;
        --force)           FORCE=true ;;
        --help|-h)
            echo "Usage: $0 [debug|release|all] [--force]"
            echo ""
            echo "  debug       Build debug deps only"
            echo "  release     Build release deps only"
            echo "  all         Build both (default)"
            echo "  --force     Rebuild even if manifest is up to date"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Run with --help for usage."
            exit 1
            ;;
    esac
done

# --- Check which modes need building ---
SOURCE_MANIFEST="$ETIL_PROJECT_DIR/ci/deps/manifest.json"
INSTALLED_MANIFEST="$ETIL_DEPS_PREFIX/manifest.json"

if [[ ! -f "$SOURCE_MANIFEST" ]]; then
    etil_die "Source manifest not found: $SOURCE_MANIFEST"
fi

MANIFEST_STALE=false
if [[ ! -f "$INSTALLED_MANIFEST" ]] || ! diff -q "$SOURCE_MANIFEST" "$INSTALLED_MANIFEST" >/dev/null 2>&1; then
    MANIFEST_STALE=true
fi

# Determine which modes are requested
case "$MODE" in
    debug)   REQUESTED_MODES="debug" ;;
    release) REQUESTED_MODES="release" ;;
    all)     REQUESTED_MODES="debug release" ;;
esac

# Filter to only modes that need building
MODES_TO_BUILD=""
for m in $REQUESTED_MODES; do
    local_dir="$ETIL_DEPS_PREFIX/$m"
    if [[ "$FORCE" == true ]]; then
        MODES_TO_BUILD="$MODES_TO_BUILD $m"
    elif [[ "$MANIFEST_STALE" == true ]]; then
        MODES_TO_BUILD="$MODES_TO_BUILD $m"
    elif [[ ! -d "$local_dir/lib" ]]; then
        MODES_TO_BUILD="$MODES_TO_BUILD $m"
    fi
done
MODES_TO_BUILD="${MODES_TO_BUILD# }"  # trim leading space

if [[ -z "$MODES_TO_BUILD" ]]; then
    etil_log "Dependencies up to date ($ETIL_DEPS_PREFIX)"
    exit 0
fi

# --- Build needed modes ---
for m in $MODES_TO_BUILD; do
    etil_log "Building $m dependencies → $ETIL_DEPS_PREFIX/$m"
    "$ETIL_PROJECT_DIR/ci/deps/build-deps.sh" "$ETIL_DEPS_PREFIX" "$m"
done

etil_log "Dependencies built successfully"
