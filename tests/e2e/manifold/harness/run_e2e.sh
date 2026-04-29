#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Thin shim — execs the Python harness so existing invocations
# (./harness/run_e2e.sh FILE) keep working. The real driver is
# run_e2e.py (typed, stdlib urllib, SSE-aware).
#
# Kept as a separate file so muscle-memory `run_e2e.sh` paths in
# notes / READMEs / CI still resolve. Will be removed in a future
# cleanup once all callers reference the .py directly.

set -euo pipefail
exec "$(dirname -- "$0")/run_e2e.py" "$@"
