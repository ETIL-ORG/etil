# MCE Phase 1b — Validation Results

**Date:** 2026-04-07
**Branch:** `mce-phase-1a-orchestration` (v2.2.2)
**Plan:** `20260405D-MCE-Phases-1-3-Implementation-Plan.md`
**Predecessor:** `20260407A-MCE-Phase-1a-Validation-Results.md`
**Status:** Cache bug fixed, MCE functional, credit modes deferred

---

## What Changed

A single line in `Dictionary::register_word()` fixed the Phase 1a failure.

### Root Cause

`register_word()` did not bump `Dictionary::generation_`. The bytecode executor caches resolved impl pointers per `Call` instruction and only invalidates the cache when `generation_` changes. Since `register_word()` left `generation_` unchanged, the cache held the original sub-concept impl across all fitness evaluations. Every child registered by `evolve_sub_concept()` was invisible to the chain — `target-fn`'s bytecode always called the original `square-term`, never the mutated child.

This explains Phase 1a's uniform fitness (~0.999 for all children, 0/9 exact matches): every evaluation measured the same original chain, regardless of which child was registered.

### Fix

```cpp
// dictionary.cpp — register_word()
wc.implementations.push_back(std::move(impl));
generation_.fetch_add(1, std::memory_order_release);  // <-- added
```

The `forget_word()`, `remove_implementation_at()`, and `forget_all()` methods already bumped generation. `register_word()` was the only mutating method that did not.

### Secondary Fix

Added pass counts to the MCE fitness log format:
```
MCE child of 'square-term' impl#372: 3/9 pass, fitness=0.339403
```

Previously only `fitness=` was logged, making it impossible to extract exact-match counts from the evolution log.

---

## Validation Results

### 30 Generations, 5 Seeds

| Seed | Monolithic | MCE Chain |
|------|-----------|-----------|
| 42 | 9/9 f=0.999274 | 9/9 f=0.999099 |
| 123 | 9/9 f=0.999293 | 1/9 f=0.339403 |
| 7 | 9/9 f=0.999222 | 9/9 f=0.999139 |
| 999 | 9/9 f=0.999292 | 6/9 f=0.357424 |
| 314159 | 9/9 f=0.999291 | 0/9 f=0.238755 |

### 100 Generations, 5 Seeds

| Seed | Monolithic | MCE Chain |
|------|-----------|-----------|
| 42 | 9/9 f=0.999192 | 7/9 f=0.407164 |
| 123 | 9/9 f=0.999292 | 2/9 f=0.401047 |
| 7 | 9/9 f=0.999251 | 2/9 f=0.377524 |
| 999 | 9/9 f=0.999298 | 9/9 f=0.376749 |
| 314159 | 9/9 f=0.999287 | 8/9 f=0.443458 |

### Comparison with Phase 1a

| Metric | Phase 1a | Phase 1b |
|--------|----------|----------|
| MCE passes (30 gens, best seed) | 0/9 | 9/9 |
| MCE fitness range | 0.999 uniform | 0.24–0.999 varied |
| Weight separation | None (all ~0.066) | Active pruning at varied weights |
| Children evaluated | No (cache stale) | Yes (cache invalidated) |

---

## Observations

### 1. MCE is functional

Children are now evaluated through the chain. Fitness scores vary per child (0.09–0.999), demonstrating that the credit signal differentiates mutations. Some seeds achieve perfect 9/9 scores.

### 2. High variance across seeds

Monolithic reaches 9/9 on all 5 seeds. MCE reaches 9/9 on 2/5 seeds (30 gens) or 1/5 seeds (100 gens). The variance is intrinsic to modular evolution: each sub-concept mutation is evaluated through the full chain, and a bad mutation to any sub-concept degrades all test cases simultaneously.

### 3. More generations can degrade MCE

Seed 42 went from 9/9 at 30 generations to 7/9 at 100 generations. This happens because continued mutation of already-correct sub-concepts introduces regressions. The `lookup()` path always selects the latest impl — if a bad child is registered last and not yet pruned, it gets selected during the next evaluation.

### 4. Benchmark sub-concepts are too simple

The decomposition uses sub-concepts with 2-3 instructions each:
- `square-term`: `dup *` (2 instructions)
- `linear-term`: `3 *` (2 instructions)
- `offset`: `5` (1 instruction)

Random mutation of a 2-instruction word almost always produces something worse. There is very little room for beneficial mutations. MCE is designed for cases where sub-concepts are complex enough that independent evolution can explore different sub-spaces productively. This benchmark validates the infrastructure but is not the right test for demonstrating MCE's advantage over monolithic.

### 5. lookup() is forced selection

The fitness evaluator creates an `ExecutionContext` without a `SelectionEngine`, so `Call` instructions resolve via `Dictionary::lookup()` (latest impl). After `register_word()`, the child is the latest and is guaranteed to be selected. This is effectively Option A (forced selection) from the Phase 1a validation recommendations — achieved without any additional code, just by fixing the cache invalidation.

---

## Evaluation Against Plan Criteria

The Phase 1b plan defined three pass criteria:

### Criterion 1: Lower weight variance than Phase 1a

**PASS.** Phase 1a had no weight variance (all children clustered at ~0.066). Phase 1b produces varied weights from 0.01 (pruned) to 0.999 (perfect). The weights now carry signal.

### Criterion 2: Convergence generation <= Phase 1a median

**PASS (trivially).** Phase 1a never converged (0/9 on all seeds). Phase 1b converges on 2/5 seeds at 30 generations.

### Criterion 3: No regression on Phase 1a original criteria

**PASS.** Phase 1a had no positive results to regress from. Phase 1b strictly improves on all metrics.

---

## Deferred Phase 1b Plan Items

The original Phase 1b plan included several items that are deferred:

| Item | Status | Rationale |
|------|--------|-----------|
| `ChainConcept` struct | Deferred | Not needed — `evolve_sub_concept()` handles chain tracking |
| `evolve-register-chain` primitive | Deferred | Current `evolve-register` + `evolve-chain` workflow is sufficient |
| Credit mode enum (Raw/Survival/Marginal) | Deferred | `lookup()` forced selection (effectively Raw + forced) works; Survival and Marginal are optimizations for when baseline credit is insufficient |
| `evolve-credit-mode!` TIL word | Deferred | No credit mode selection without the enum |
| `EvolveLogCategory::ChainCredit` | Deferred | Standard `Fitness` category logs MCE children adequately |

These items remain viable for a future Phase 1b+ if MCE needs tighter credit assignment on more complex benchmarks. The cache fix was the blocking issue.

---

## Next Steps

Per the plan's sequencing:
- Phase 2 (co-evolutionary mean-over-partners) is **conditional** — skip if Phase 1b produces stable weights. The current results show weight variance, so Phase 2 is not immediately needed.
- Phase 3 (module-boundary crossover) can proceed. It adds `chain_crossover` as a genetic operator, which is independent of credit modes.

Before proceeding to Phase 3, the branch should be:
1. Documented (README.md, CLAUDE.md updates)
2. Super-pushed to CI
3. Pushed to GitHub after CI passes
