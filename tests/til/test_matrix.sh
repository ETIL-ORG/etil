#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
REPL="${ETIL_REPL:-$PROJECT_DIR/build-debug/bin/etil_repl}"

if [[ ! -x "$REPL" ]]; then
    echo "REPL not found at $REPL" >&2
    exit 1
fi

# Check if linalg is enabled by testing for mat-new word
OUTPUT=$(cd "$PROJECT_DIR" && echo "mat-new" | "$REPL" --quiet 2>&1) || true
if echo "$OUTPUT" | grep -q "Unknown word"; then
    echo "--- Skipping matrix tests (ETIL_BUILD_LINALG not enabled) ---"
    exit 0
fi

OUTPUT=$(cd "$PROJECT_DIR" && "$REPL" --quiet 2>/dev/null <<'EOF'
include tests/til/test_matrix.til
/quit
EOF
)

echo "$OUTPUT"

if echo "$OUTPUT" | grep -q "FAIL"; then
    echo "--- TIL MATRIX TEST FAILED ---"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "PASS"; then
    echo "--- TIL MATRIX TEST: no PASS output ---"
    exit 1
fi

echo "--- All matrix tests passed ---"
