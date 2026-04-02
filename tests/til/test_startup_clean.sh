#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Verify the REPL starts cleanly — no errors from builtins.til or help.til.
# This catches parse errors, missing words, and broken examples in startup files.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
REPL="${ETIL_REPL:-$PROJECT_DIR/build-debug/bin/etil_repl}"

if [[ ! -x "$REPL" ]]; then
    echo "REPL not found at $REPL" >&2
    exit 1
fi

# Start the REPL in quiet mode, immediately quit.
# Capture both stdout and stderr — startup errors can appear on either.
OUTPUT=$(cd "$PROJECT_DIR" && "$REPL" --quiet 2>&1 <<'EOF'
/quit
EOF
)

ERRORS=""

if echo "$OUTPUT" | grep -qi "error"; then
    ERRORS="$ERRORS$(echo "$OUTPUT" | grep -i "error")"$'\n'
fi

if echo "$OUTPUT" | grep -q "Unknown word"; then
    ERRORS="$ERRORS$(echo "$OUTPUT" | grep "Unknown word")"$'\n'
fi

if [ -n "$ERRORS" ]; then
    echo "FAIL: REPL startup produced errors:"
    echo "$ERRORS"
    echo "--- Startup health check FAILED ---"
    exit 1
fi

echo "PASS: REPL startup clean (no errors)"
echo "--- Startup health check passed ---"
