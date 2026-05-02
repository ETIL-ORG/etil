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

OUTPUT=$(cd "$PROJECT_DIR" && "$REPL" --quiet --data-dir "$PROJECT_DIR/data" 2>/dev/null <<'EOF'
include tests/til/test_pipeline_wait_timeout.til
/quit
EOF
)

echo "$OUTPUT"

# The .til prints `ELAPSED-US <microseconds>` on a line of its own.
ELAPSED=$(echo "$OUTPUT" | awk '/^ELAPSED-US/ { print int($2); exit }')
if [[ -z "${ELAPSED:-}" ]]; then
    echo "FAIL — no ELAPSED-US marker in REPL output"
    exit 1
fi

# Acceptable window: 200_000us .. 500_000us (0.2s .. 0.5s).
# The deadline is set to 0.3s; libuv loop tick + scheduler jitter widen
# the upper bound a little.
if (( ELAPSED < 200000 || ELAPSED > 500000 )); then
    echo "FAIL — elapsed ${ELAPSED}us outside expected 200000..500000us window"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "^DONE$"; then
    echo "FAIL — DONE marker missing (test did not complete)"
    exit 1
fi

echo "PASS — pipeline-wait-timeout bounded async pipeline at ${ELAPSED}us"
