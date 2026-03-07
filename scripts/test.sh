#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Run ETIL tests (debug, release, or both).
#
# Usage: test.sh [debug|release|all] [--filter PATTERN] [--parallel N]
#   debug       Test debug build (default)
#   release     Test release build
#   all         Test both
#   --filter    CTest -R regex filter
#   --parallel  CTest -j level (default: number of CPUs)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

etil_require_cmd ctest

# --- Parse args ---
TARGET="debug"
FILTER=""
PARALLEL=$(nproc 2>/dev/null || echo 4)

while [[ $# -gt 0 ]]; do
    case "$1" in
        debug|release|all) TARGET="$1"; shift ;;
        --filter)
            if [[ -z "${2:-}" ]]; then
                echo "Error: --filter requires a pattern argument"
                exit 1
            fi
            FILTER="$2"; shift 2 ;;
        --parallel)
            if [[ -z "${2:-}" ]]; then
                echo "Error: --parallel requires a number argument"
                exit 1
            fi
            PARALLEL="$2"; shift 2 ;;
        --help|-h)
            echo "Usage: $0 [debug|release|all] [--filter PATTERN] [--parallel N]"
            echo ""
            echo "  debug       Test debug build (default)"
            echo "  release     Test release build"
            echo "  all         Test both"
            echo "  --filter    CTest -R regex filter"
            echo "  --parallel  CTest -j level (default: $(nproc 2>/dev/null || echo 4))"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Run with --help for usage."
            exit 1
            ;;
    esac
done

# --- Test function for a single target ---
test_target() {
    local label="$1"
    local build_dir="$2"

    if [[ ! -d "$build_dir" ]]; then
        etil_die "$label build directory not found: $build_dir (run build.sh $label first)"
    fi

    etil_log "Testing $label → $build_dir"

    local ctest_args=(
        --test-dir "$build_dir"
        --output-on-failure
        -j "$PARALLEL"
    )

    if [[ -n "$FILTER" ]]; then
        ctest_args+=(-R "$FILTER")
    fi

    local start=$SECONDS
    ctest "${ctest_args[@]}"
    local elapsed=$((SECONDS - start))
    etil_log "$label tests completed in ${elapsed}s"
}

# --- Execute ---
etil_log "ETIL v$ETIL_VERSION — test target: $TARGET"

case "$TARGET" in
    debug)
        test_target "debug" "$ETIL_BUILD_DEBUG_DIR"
        ;;
    release)
        test_target "release" "$ETIL_BUILD_RELEASE_DIR"
        ;;
    all)
        test_target "debug" "$ETIL_BUILD_DEBUG_DIR"
        test_target "release" "$ETIL_BUILD_RELEASE_DIR"
        ;;
esac

etil_log "All tests passed."
