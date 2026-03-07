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

# Test abort via REPL heredoc — each line is a separate interpret_line()
# call, so abort recovery happens between lines.
OUTPUT=$(cd "$PROJECT_DIR" && "$REPL" --quiet <<'EOF'
include tests/til/harness.til
42 true abort 99
." abort-stops-tokens: " depth 1 expect-eq
drop
: test-abort-inner 10 true abort 20 ;
test-abort-inner
." abort-in-compiled: " depth 1 expect-eq
drop
." before" true abort ." after"
." output-before-abort: " depth 0 expect-eq
s" error msg" false abort
." error-abort-recovers: " depth 0 expect-eq
/quit
EOF
)

echo "$OUTPUT"

if echo "$OUTPUT" | grep -q "FAIL"; then
    echo "--- TIL TEST FAILED ---"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "PASS"; then
    echo "--- TIL TEST ERROR: no PASS output ---"
    exit 1
fi

echo "--- All til tests passed ---"
