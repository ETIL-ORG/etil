#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# MCE / ConceptDAG validation — 3-way comparison across 5 seeds.
#
# Monolithic: evolve-word on the root word directly
# MCE-chain:  evolve-chain round-robin scheduling over sub-concepts
# MCE-DAG:    evolve-dag contribution-weighted scheduling over sub-concepts
#
# Supports two benchmark suites:
#
#   validate_mce.sh quad    — quadratic (x^2 + 3x + 5), 3 trivial sub-concepts
#   validate_mce.sh tierb   — integer norm, sqrt-approx is the heavy lifter
#   validate_mce.sh all     — run both suites (default)
#
# For each benchmark and seed, the script generates a temp copy with
# the seed and log directory substituted, runs it, and parses the
# evolution log for the best fitness and highest test-pass count
# observed.

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
SUITE="${1:-all}"

# --- Helpers ---

run_benchmark() {
    # $1 = template path, $2 = seed, $3 = log dir
    local template=$1 seed=$2 logdir=$3
    local tmp
    tmp=$(mktemp /tmp/bench_XXXXXX.til)

    sed -e "s|^42 evolve-seed!|$seed evolve-seed!|" \
        -e "s|s\" /tmp/\" evolve-log-dir|s\" $logdir/\" evolve-log-dir|" \
        "$template" > "$tmp"

    rm -f "$logdir"/*.log
    ( cd "$PROJECT_DIR" && \
      printf 'include %s\n' "$tmp" | \
      timeout 300 "$REPL" --quiet --data-dir data >/dev/null 2>/dev/null ) || true

    rm -f "$tmp"
}

best_fitness() {
    local log=$1
    grep -oE 'fitness=[0-9.]+' "$log" 2>/dev/null \
        | sed 's/fitness=//' \
        | sort -rn \
        | head -1
}

max_passes() {
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

run_suite() {
    # $1 = suite label, $2 = monolithic .til, $3 = chain .til, $4 = dag .til, $5 = total tests
    local label=$1 mono=$2 chain=$3 dag=$4 total=$5

    local mono_log=/tmp/validate-$label-mono
    local chain_log=/tmp/validate-$label-chain
    local dag_log=/tmp/validate-$label-dag
    mkdir -p "$mono_log" "$chain_log" "$dag_log"

    echo ""
    echo "=== $label benchmark ==="
    echo ""
    printf "%-8s  %-18s  %-18s  %-18s\n" "Seed" "Monolithic" "MCE-chain" "MCE-DAG"
    printf "%-8s  %-18s  %-18s  %-18s\n" "----" "----------" "---------" "-------"

    for seed in "${SEEDS[@]}"; do
        run_benchmark "$SCRIPT_DIR/$mono"  "$seed" "$mono_log"
        run_benchmark "$SCRIPT_DIR/$chain" "$seed" "$chain_log"
        run_benchmark "$SCRIPT_DIR/$dag"   "$seed" "$dag_log"

        printf "%-8s  %-18s  %-18s  %-18s\n" \
            "$seed" \
            "$(fmt_result "$mono_log" "$total")" \
            "$(fmt_result "$chain_log" "$total")" \
            "$(fmt_result "$dag_log"   "$total")"
    done
}

# --- Main ---

echo "=== MCE / ConceptDAG Validation ==="
echo "Seeds: ${SEEDS[*]}"

case "$SUITE" in
    quad)
        run_suite "quadratic" \
            bench_mce_monolithic.til \
            bench_mce_quad.til \
            bench_dag_quad.til \
            9
        ;;
    tierb)
        run_suite "tier-b-integer-norm" \
            bench_mce_tierb_monolithic.til \
            bench_mce_tierb_chain.til \
            bench_dag_tierb.til \
            9
        ;;
    all)
        run_suite "quadratic" \
            bench_mce_monolithic.til \
            bench_mce_quad.til \
            bench_dag_quad.til \
            9
        run_suite "tier-b-integer-norm" \
            bench_mce_tierb_monolithic.til \
            bench_mce_tierb_chain.til \
            bench_dag_tierb.til \
            9
        ;;
    *)
        echo "Usage: $0 [quad|tierb|all]" >&2
        exit 1
        ;;
esac

echo ""
echo "Legend: N/9 = best test-pass count observed, f=X.XX = best fitness observed"
echo ""
echo "Quadratic:  f(x) = x^2 + 3x + 5 — 3 trivial sub-concepts (1-3 instr each)"
echo "Tier B:     f(x,y,z) = floor(sqrt(x^2+y^2+z^2)) — sqrt-approx is 15+ instr"
