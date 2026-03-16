#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Debug-first development workflow.
# Builds and tests debug by default; add --release for both.
#
# Usage: dev.sh [--release] [--filter PATTERN] [--parallel N]
#   --release   Also build and test release
#   --filter    CTest -R regex filter (passed to test.sh)
#   --parallel  CTest -j level (passed to test.sh)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

# --- Parse args ---
ALSO_RELEASE=false
TEST_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release)  ALSO_RELEASE=true; shift ;;
        --filter)
            if [[ -z "${2:-}" ]]; then
                echo "Error: --filter requires a pattern argument"
                exit 1
            fi
            TEST_ARGS+=(--filter "$2"); shift 2 ;;
        --parallel)
            if [[ -z "${2:-}" ]]; then
                echo "Error: --parallel requires a number argument"
                exit 1
            fi
            TEST_ARGS+=(--parallel "$2"); shift 2 ;;
        --help|-h)
            echo "Usage: $0 [--release] [--filter PATTERN] [--parallel N]"
            echo ""
            echo "  --release   Also build and test release"
            echo "  --filter    CTest -R regex filter"
            echo "  --parallel  CTest -j level"
            echo ""
            echo "Default: build and test debug only."
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Run with --help for usage."
            exit 1
            ;;
    esac
done

# --- Debug build + test ---
etil_log "ETIL v$ETIL_VERSION — dev workflow"

etil_log "=== Debug build ==="
"$SCRIPT_DIR/build.sh" debug

etil_log "=== Debug test ==="
"$SCRIPT_DIR/test.sh" debug "${TEST_ARGS[@]+"${TEST_ARGS[@]}"}"

# --- Release build + test (optional) ---
if [[ "$ALSO_RELEASE" == true ]]; then
    etil_log "=== Release build ==="
    "$SCRIPT_DIR/build.sh" release

    etil_log "=== Release test ==="
    "$SCRIPT_DIR/test.sh" release "${TEST_ARGS[@]+"${TEST_ARGS[@]}"}"
fi

etil_log "Dev workflow complete."
