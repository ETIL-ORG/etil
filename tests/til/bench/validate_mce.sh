#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# MCE validation — two benchmarks, three selection modes, 5 seeds.

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

run_til() {
    (cd "$PROJECT_DIR" && echo "include $1
/quit" | timeout 120 "$REPL" --quiet 2>/dev/null >/dev/null) || true
}

best_fitness() {
    grep -o 'fitness=[0-9.]*' "$1" | sed 's/fitness=//' | sort -rn | head -1
}

max_passes() {
    local total=$1; shift
    grep -oP "[0-9]+/$total pass" "$1" | sed "s|/$total pass||" | sort -rn | head -1
}

fmt_result() {
    local log_dir=$1 total=$2
    local log_file
    log_file=$(ls -t "$log_dir"/*.log 2>/dev/null | head -1)
    if [[ -n "$log_file" ]]; then
        local bp fp
        bp=$(max_passes "$total" "$log_file")
        fp=$(best_fitness "$log_file")
        echo "${bp:-0}/$total f=${fp:-?}"
    else
        echo "no log"
    fi
}

run_benchmark() {
    local name=$1 mono_til=$2 chain_til=$3 wr_til=$4 total=$5
    local mono_log=/tmp/mce-mono-${name}-log
    local chain_log=/tmp/mce-chain-${name}-log
    local wr_log=/tmp/mce-wr-${name}-log

    mkdir -p "$mono_log" "$chain_log" "$wr_log"

    echo ""
    echo "=== Benchmark: $name ==="
    echo ""
    printf "%-8s  %-16s  %-16s  %-16s\n" "Seed" "Monolithic" "MCE-lookup" "MCE-weighted"
    printf "%-8s  %-16s  %-16s  %-16s\n" "----" "----------" "----------" "------------"

    for seed in "${SEEDS[@]}"; do
        rm -f "$mono_log"/*.log "$chain_log"/*.log "$wr_log"/*.log
        echo "$seed constant SEED" > /tmp/mce_seed.til

        run_til "$mono_til"
        run_til "$chain_til"
        run_til "$wr_til"

        printf "%-8s  %-16s  %-16s  %-16s\n" \
            "$seed" \
            "$(fmt_result "$mono_log" "$total")" \
            "$(fmt_result "$chain_log" "$total")" \
            "$(fmt_result "$wr_log" "$total")"
    done
}

echo "=== MCE Selection Mode Comparison ==="
echo "Seeds: ${SEEDS[*]}"

run_benchmark "quad" \
    /tmp/mce_mono.til /tmp/mce_chain.til /tmp/mce_chain_wr.til 9

run_benchmark "xtype" \
    /tmp/mce_mono_xtype.til /tmp/mce_chain_xtype.til /tmp/mce_chain_xtype_wr.til 11

echo ""
echo "quad:  f(x) = x^2+3x+5 — Integer→Integer homogeneous (30 gens)"
echo "xtype: f(x) = slength(number->string(x^2+1)) — Integer→String→Integer (50 gens)"
echo ""
echo "Monolithic:   evolve-word on pipeline directly"
echo "MCE-lookup:   evolve-sub with lookup() = latest impl (deterministic)"
echo "MCE-weighted: evolve-sub with weighted-random (runtime behavior)"
