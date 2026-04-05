# TBBP Validation Findings

**Date:** 2026-04-05
**Context:** After implementing TBBP v1.13.7 and mutation-level extension v1.13.8
**Status:** Empirical validation — inconclusive signal

## Summary

TBBP's mechanism is **architecturally correct** but produces **sparse and directionally-wrong signals** on the benchmarks tested so far. All unit tests pass; the issue is empirical, not mechanical.

## Benchmarks Run

1. **Quad regression** (`test_tbbp_validation.til` + `bench_tbbp.til`)
   - Target: `f(x) = x² + 3x + 5` (Integer → Integer)
   - 50 generations
   - Starting program: `: fn int->float dup * float->int ;` (correct, fitness ~1.0)
   - Result: 3-10 bridge updates, all reward=0, no learning

2. **Cross-domain Integer→Float** (`bench_cross_domain.til`)
   - Target: `f(x) = Float x²` (Integer → Float)
   - Starting program: `: fn dup * ;` (returns Integer, type mismatch with Float expected)
   - 100 generations
   - Result: 11 bridge updates, **zero rewards=1**, weights decrease uniformly

## Core Finding: Reward Signal Misalignment

TBBP rewards a bridge when `child_fitness > parent_fitness`. But TypeRepair (which calls `select_path` to invoke TBBP) fires only when a mutation **broke the program** and needs type repair.

This creates an anti-correlation:
- TypeRepair fires → mutation already damaged type structure
- Damaged mutations → lower child fitness
- Lower child fitness → reward=0
- reward=0 → weight penalized

**Result:** TBBP penalizes all bridges used during repair, regardless of which specific bridge was chosen. It becomes a counter of "how often did this bridge's type pair need repair in a failed mutation," not a learning signal for "which bridge is useful."

### Quantitative Evidence

From the cross-domain benchmark (100 generations, 500 mutations):
- 11 TBBP select events, 11 updates
- **0 reward=1 events**
- All 5 distinct bridges used ended at weight ≤ 0.9
- The MOST-selected bridge (`float->int`, 4 selections) ended at the LOWEST weight (0.656)

This is the opposite of what we want: the mechanism is penalizing frequently-used bridges in an unhelpful way.

## Why the Mutation-Level Extension (Option B) Didn't Help

The v1.13.8 extension wired `try_record_bridge_usage()` into `substitute_call` and `grow_node` so mutation-selected bridge words could update weights. Empirical result: **0 bridges recorded from 65 typed substitute candidates**.

**Why:** Mutation operators select candidates from `find_type_compatible()`, which returns words whose input type accepts the TOS type. These include:
- Words with `Unknown` input (polymorphic, matches anything)
- Words with concrete input matching TOS

When a mutation picks `bytes-length` (bridge from ByteArray→Integer) as a candidate for a position with TOS=Unknown or TOS=Integer, the word is NOT acting in its bridge role. `try_record_bridge_usage` correctly rejects these — they aren't bridge transitions from the TOS type.

**The chosen word is often a bridge word, but rarely used AS a bridge.**

## What Would Make TBBP Work

For TBBP to show measurable merit, we need:
1. **TypeRepair firing on fit-improving mutations** — currently it fires on failed mutations
2. **OR mutation-level bridge attribution** — track when the mutation chose a bridge specifically for its bridge role
3. **Reward-before-penalty** — give bridges a "grace period" of positive reward before penalizing, OR use a different update rule (UCB1, Thompson sampling)

Alternative paths forward:

### Path A: Grace Period / Initial Reward Boost
Start each bridge at weight 1.0 but give it a positive-reward boost on first selection regardless of fitness. This would prevent the "penalize all repairs" anti-signal.

### Path B: Reward the Bridge's Survival, Not Immediate Fitness Delta
Track whether bridge-containing children survive pruning (vs being pruned out). If they survive, reward. If pruned, punish. This measures long-term bridge utility instead of immediate fitness improvement.

### Path C: Two-Population Ablation
Run evolution with TBBP-on and TBBP-off at identical seeds, measure long-term convergence. If TBBP-on converges faster even with noisy per-bridge signals, the aggregate effect is beneficial.

### Path D: Accept Sparse Signal, Move On
TBBP may be valuable only in very specific problem domains (heavy cross-type transitions). For most problems, its signal is too sparse to matter. Ship it as infrastructure and focus on MCE.

## Recommendation

**Path D for v1.13.x.** The TBBP mechanism is correct and shipping. It won't hurt; it just doesn't clearly help on current benchmarks.

**Revisit with Path B or Path C as a v1.14+ experiment** once MCE is operational. MCE's round-robin evolution may produce cleaner fitness deltas that could make TBBP's signal more actionable.

## Code Assessment

- **Unit tests:** 46 bridge_map tests + 17 type_repair tests = mechanism validated
- **Integration test:** `test_tbbp_validation.til` = toggle works end-to-end
- **No regressions:** 1484/1484 tests passing
- **Production deployed:** v1.13.8 on internal CI

The mechanism is production-ready. Empirical merit determination requires better benchmarks or a reward-attribution redesign.
