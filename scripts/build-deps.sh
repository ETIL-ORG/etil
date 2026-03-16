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

# --- Check if deps are up to date ---
SOURCE_MANIFEST="$ETIL_PROJECT_DIR/ci/deps/manifest.json"
INSTALLED_MANIFEST="$ETIL_DEPS_PREFIX/manifest.json"

if [[ ! -f "$SOURCE_MANIFEST" ]]; then
    etil_die "Source manifest not found: $SOURCE_MANIFEST"
fi

if [[ "$FORCE" == false && -f "$INSTALLED_MANIFEST" ]]; then
    if diff -q "$SOURCE_MANIFEST" "$INSTALLED_MANIFEST" >/dev/null 2>&1; then
        etil_log "Dependencies up to date ($ETIL_DEPS_PREFIX)"
        exit 0
    fi
    etil_log "Manifest changed — rebuilding dependencies"
else
    if [[ "$FORCE" == true ]]; then
        etil_log "Force rebuild requested"
    else
        etil_log "No installed manifest — building dependencies"
    fi
fi

# --- Build ---
etil_log "Building dependencies: mode=$MODE prefix=$ETIL_DEPS_PREFIX"

"$ETIL_PROJECT_DIR/ci/deps/build-deps.sh" "$ETIL_DEPS_PREFIX" "$MODE"

etil_log "Dependencies built successfully"
