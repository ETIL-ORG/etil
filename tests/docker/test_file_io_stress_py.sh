#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Wrapper for Python E2E File I/O Stress Tests
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

: "${ETIL_TEST_API_KEY:?ETIL_TEST_API_KEY is required}"

exec python3 "$SCRIPT_DIR/test_file_io_stress.py" "$@"
