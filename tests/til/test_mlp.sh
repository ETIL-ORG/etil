#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

REPL="${ETIL_REPL:?ETIL_REPL not set}"
OUTPUT=$("$REPL" --quiet < "${BASH_SOURCE[0]%.*}.til" 2>&1)

echo "$OUTPUT"

if echo "$OUTPUT" | grep -qi "FAIL"; then
    echo "FAIL detected"
    exit 1
fi

if ! echo "$OUTPUT" | grep -qi "PASS"; then
    echo "No PASS found"
    exit 1
fi

echo "All tests passed."
