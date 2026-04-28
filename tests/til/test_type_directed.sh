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

TMPDIR_RUN=$(mktemp -d -t til-type-directed-XXXXXX)
trap 'rm -rf "$TMPDIR_RUN"' EXIT
STDOUT_FILE="$TMPDIR_RUN/stdout"
STDERR_FILE="$TMPDIR_RUN/stderr"

set +e
( cd "$PROJECT_DIR" && "$REPL" --quiet >"$STDOUT_FILE" 2>"$STDERR_FILE" <<'EOF'
include tests/til/test_type_directed.til
/quit
EOF
)
RC=$?
set -e

OUTPUT=$(cat "$STDOUT_FILE")
ERRORS=$(cat "$STDERR_FILE")

echo "$OUTPUT"

dump_diagnostics() {
    local why="$1"
    echo "--- TIL TYPE-DIRECTED TEST $why ---"
    echo "REPL: $REPL"
    echo "REPL exit code: $RC"
    echo "stdout bytes: $(wc -c < "$STDOUT_FILE")"
    echo "stderr bytes: $(wc -c < "$STDERR_FILE")"
    echo "--- stdout (cat -v) ---"
    cat -v "$STDOUT_FILE"
    echo "--- stderr (cat -v) ---"
    cat -v "$STDERR_FILE"
    echo "--- end diagnostics ---"
}

if echo "$OUTPUT" | grep -q "FAIL"; then
    dump_diagnostics "FAILED (FAIL marker in stdout)"
    exit 1
fi

if ! echo "$OUTPUT" | grep -q "PASS"; then
    dump_diagnostics "ERROR: no PASS marker in stdout"
    exit 1
fi

if [[ $RC -ne 0 ]]; then
    dump_diagnostics "ERROR: REPL exited non-zero ($RC)"
    exit 1
fi

echo "--- All type-directed bridge tests passed ---"
