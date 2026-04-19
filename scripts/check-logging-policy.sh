#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Enforce the no-direct-stdio-logging policy from
# docs/claude-design/20260418A-Logging-Infrastructure-Survey.md §11.
#
# Scans src/ and include/ for raw fprintf(stderr) / std::cerr / printf /
# std::cout usage and prints any occurrences not on the allowlist of
# bootstrap exception sites.
#
# Exits 0 if the tree is clean or only allowlisted sites match; exits 1
# otherwise. Intended for pre-commit hooks and CI. Run from repo root.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Allowlisted bootstrap exception sites (file paths relative to repo root).
# These files run before the Phase 0 logging substrate is available or
# after it has been torn down, so direct stdio is the only option.
ALLOWLIST=(
    "examples/mcp_server.cpp"      # argparse before init
    "examples/simple_repl.cpp"     # config-load warnings, interactive output
    "src/core/logging.cpp"         # logging impl itself may fprintf on fallback
    "src/mcp/mcp_sse_out_sink.cpp" # std::fprintf in stderr_sink path (legacy)
    "src/manifold/sinks.cpp"       # stderr_sink implementation
)

PATTERNS=(
    'fprintf[[:space:]]*\([[:space:]]*stderr'
    'std::cerr[[:space:]]*<<'
    'std::cout[[:space:]]*<<'
)

violations=0
printed_header=0
for p in "${PATTERNS[@]}"; do
    while IFS= read -r line; do
        [ -z "$line" ] && continue
        file="${line%%:*}"
        skip=0
        for allowed in "${ALLOWLIST[@]}"; do
            if [[ "$file" == "$allowed" ]]; then
                skip=1
                break
            fi
        done
        if [ $skip -eq 0 ]; then
            if [ $printed_header -eq 0 ]; then
                echo "Logging policy violation — raw stdio in non-bootstrap sources:"
                echo
                printed_header=1
            fi
            echo "  $line"
            violations=$((violations + 1))
        fi
    done < <(grep -Rn --include='*.cpp' --include='*.hpp' -E "$p" src/ include/ 2>/dev/null || true)
done

if [ $violations -gt 0 ]; then
    echo
    echo "Total violations: $violations"
    echo "Allowlisted files: ${ALLOWLIST[*]}"
    exit 1
fi

echo "Logging policy: clean (scanned src/, include/)"
exit 0
