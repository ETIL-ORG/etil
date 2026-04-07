#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# MCE Phase 1a validation — compare monolithic vs MCE chain evolution
# across 5 seeds. Extracts max fitness from evolution logs.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
WORKSPACE_DIR="$(cd "$PROJECT_DIR/.." && pwd)"
REPL="${ETIL_REPL:-$WORKSPACE_DIR/build-debug/bin/etil_repl}"

if [[ ! -x "$REPL" ]]; then
    echo "REPL not found at $REPL — build debug first" >&2
    exit 1
fi

SEEDS=(42 123 7 999 314159)
GENS=${1:-30}
MONO_LOG=/tmp/mce-mono-log
CHAIN_LOG=/tmp/mce-chain-log

mkdir -p "$MONO_LOG" "$CHAIN_LOG"

best_fitness() {
    # Extract the highest fitness= value from a log file
    grep -o 'fitness=[0-9.]*' "$1" | sed 's/fitness=//' | sort -rn | head -1
}

max_passes() {
    # Extract the best "N/M pass" from a log file
    grep -o '[0-9]*/9 pass' "$1" | sed 's|/9 pass||' | sort -rn | head -1
}

echo "=== MCE Phase 1a Validation ==="
echo "Target: f(x) = x^2 + 3x + 5"
echo "Generations: $GENS"
echo ""
printf "%-8s  %-16s  %-16s\n" "Seed" "Monolithic" "MCE Chain"
printf "%-8s  %-16s  %-16s\n" "----" "----------" "---------"

for seed in "${SEEDS[@]}"; do
    # Clean logs
    rm -f "$MONO_LOG"/*.log "$CHAIN_LOG"/*.log

    # Write seed file
    echo "$seed constant SEED" > /tmp/mce_seed.til

    # Run monolithic (must cd to project dir for builtins.til)
    (cd "$PROJECT_DIR" && echo "include /tmp/mce_mono.til
/quit" | timeout 120 "$REPL" --quiet 2>/dev/null >/dev/null) || true

    # Run MCE chain
    (cd "$PROJECT_DIR" && echo "include /tmp/mce_chain.til
/quit" | timeout 120 "$REPL" --quiet 2>/dev/null >/dev/null) || true

    # Extract results from logs
    mono_log=$(ls -t "$MONO_LOG"/*.log 2>/dev/null | head -1)
    chain_log=$(ls -t "$CHAIN_LOG"/*.log 2>/dev/null | head -1)

    if [[ -n "$mono_log" ]]; then
        mono_best=$(best_fitness "$mono_log")
        mono_pass=$(max_passes "$mono_log")
        mono_result="${mono_pass:-0}/9 f=${mono_best:-?}"
    else
        mono_result="no log"
    fi

    if [[ -n "$chain_log" ]]; then
        chain_best=$(best_fitness "$chain_log")
        chain_pass=$(max_passes "$chain_log")
        chain_result="${chain_pass:-0}/9 f=${chain_best:-?}"
    else
        chain_result="no log"
    fi

    printf "%-8s  %-16s  %-16s\n" "$seed" "$mono_result" "$chain_result"
done

echo ""
echo "Format: best_passes/9 f=best_fitness"
