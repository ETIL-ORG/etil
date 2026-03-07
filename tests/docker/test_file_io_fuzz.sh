#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Parallel Randomized File I/O Fuzz Tests via HTTP Transport
# Wrapper for test_file_io_fuzz.py — validates env and launches Python.
#
# Usage:
#   ETIL_TEST_API_KEY="..." bash tests/docker/test_file_io_fuzz.sh
#
# Configuration (env vars):
#   ETIL_TEST_URL       MCP endpoint (default: http://localhost:8080/mcp)
#   ETIL_TEST_API_KEY   Bearer token (required)
#   ETIL_TEST_TIMEOUT   HTTP timeout in seconds (default: 30)
#   ETIL_TEST_WORKERS   Concurrent workers (default: 4)
#   ETIL_TEST_SEED      RNG seed for reproducibility (default: random)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${ETIL_TEST_API_KEY:?ETIL_TEST_API_KEY is required}"
exec python3 "$SCRIPT_DIR/test_file_io_fuzz.py" "$@"
