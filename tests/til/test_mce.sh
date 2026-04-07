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

# Capture output; allow non-zero exit from ASan leak detection in debug builds
set +e
OUTPUT=$(cd "$PROJECT_DIR" && "$REPL" --quiet 2>/dev/null <<'EOF'
include tests/til/test_mce.til
/quit
EOF
)
set -e

echo "$OUTPUT"

if echo "$OUTPUT" | grep -q "FAIL"; then
    echo "--- TIL MCE TEST FAILED ---"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "PASS"; then
    echo "--- TIL MCE TEST: no PASS output ---"
    exit 1
fi

echo "--- All MCE tests passed ---"
