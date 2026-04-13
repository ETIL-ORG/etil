#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# MCE / ConceptDAG validation — 3-way comparison across 5 seeds.
#
# Monolithic: evolve-word on target-fn directly
# MCE-chain:  evolve-chain with round-robin sub-concept scheduling
# MCE-DAG:    evolve-dag with contribution-weighted sub-concept scheduling
#
# For each benchmark and seed, the script generates a temp copy with the
# seed and log directory substituted, runs it, and parses the evolution
# log for the best fitness and highest test-pass count observed.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
WORKSPACE_DIR="$(cd "$PROJECT_DIR/.." && pwd)"
REPL="${ETIL_REPL:-$WORKSPACE_DIR/build/bin/etil_repl}"

if [[ ! -x "$REPL" ]]; then
    echo "REPL not found at $REPL — build release first" >&2
    exit 1
fi

SEEDS=(42 123 7 999 314159)
TOTAL_TESTS=9

# --- Helpers ---

run_benchmark() {
    # $1 = template path, $2 = seed, $3 = log dir
    local template=$1 seed=$2 logdir=$3
    local tmp
    tmp=$(mktemp /tmp/bench_XXXXXX.til)

    # Substitute seed and log directory without risking regex collisions
    sed -e "s|^42 evolve-seed!|$seed evolve-seed!|" \
        -e "s|s\" /tmp/\" evolve-log-dir|s\" $logdir/\" evolve-log-dir|" \
        "$template" > "$tmp"

    rm -f "$logdir"/*.log
    ( cd "$PROJECT_DIR" && \
      printf 'include %s\n' "$tmp" | \
      timeout 180 "$REPL" --quiet --data-dir data >/dev/null 2>/dev/null ) || true

    rm -f "$tmp"
}

best_fitness() {
    # Extract highest fitness value from log, or 0 if none
    local log=$1
    grep -oE 'fitness=[0-9.]+' "$log" 2>/dev/null \
        | sed 's/fitness=//' \
        | sort -rn \
        | head -1
}

max_passes() {
    # Extract highest N/TOTAL pass count observed, or 0 if none
    local log=$1 total=$2
    grep -oE "[0-9]+/$total pass" "$log" 2>/dev/null \
        | sed "s|/$total pass||" \
        | sort -rn \
        | head -1
}

fmt_result() {
    local logdir=$1 total=$2
    local log
    log=$(ls -t "$logdir"/*.log 2>/dev/null | head -1)
    if [[ -z "$log" ]]; then
        printf "%s" "no log"
        return
    fi
    local bp fp
    bp=$(max_passes "$log" "$total")
    fp=$(best_fitness "$log")
    printf "%s/%s f=%s" "${bp:-0}" "$total" "${fp:-0}"
}

# --- Main ---

echo "=== MCE / ConceptDAG Validation ==="
echo "Seeds: ${SEEDS[*]}"
echo "Target: f(x) = x^2 + 3x + 5 — Integer→Integer, 9 test cases, 100 generations"
echo ""

MONO_LOG=/tmp/validate-mce-mono
CHAIN_LOG=/tmp/validate-mce-chain
DAG_LOG=/tmp/validate-mce-dag
mkdir -p "$MONO_LOG" "$CHAIN_LOG" "$DAG_LOG"

printf "%-8s  %-18s  %-18s  %-18s\n" "Seed" "Monolithic" "MCE-chain" "MCE-DAG"
printf "%-8s  %-18s  %-18s  %-18s\n" "----" "----------" "---------" "-------"

for seed in "${SEEDS[@]}"; do
    run_benchmark "$SCRIPT_DIR/bench_mce_monolithic.til" "$seed" "$MONO_LOG"
    run_benchmark "$SCRIPT_DIR/bench_mce_quad.til"       "$seed" "$CHAIN_LOG"
    run_benchmark "$SCRIPT_DIR/bench_dag_quad.til"       "$seed" "$DAG_LOG"

    printf "%-8s  %-18s  %-18s  %-18s\n" \
        "$seed" \
        "$(fmt_result "$MONO_LOG" "$TOTAL_TESTS")" \
        "$(fmt_result "$CHAIN_LOG" "$TOTAL_TESTS")" \
        "$(fmt_result "$DAG_LOG"   "$TOTAL_TESTS")"
done

echo ""
echo "Legend: N/9 = best test-pass count observed in log, f=X.XX = best fitness"
echo ""
echo "Monolithic: evolve-word directly mutates target-fn"
echo "MCE-chain:  evolve-chain round-robin over [square-term, linear-term, offset]"
echo "MCE-DAG:    evolve-dag contribution-weighted scheduling over the same concepts"
