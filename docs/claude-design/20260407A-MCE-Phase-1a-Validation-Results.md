# MCE Phase 1a — Validation Results

**Date:** 2026-04-07
**Branch:** `mce-phase-1a-orchestration` (v2.2.1)
**Plan:** `20260405D-MCE-Phases-1-3-Implementation-Plan.md`
**Status:** Infrastructure shipped, implicit credit insufficient — proceed to Phase 1b

---

## What Was Built

Phase 1a added three new primitives and one TIL word to enable modular co-evolution:

| Word | Type | Stack Effect | Purpose |
|------|------|-------------|---------|
| `evolve-sub` | C++ primitive | `( sub-str chain-str -- n )` | Mutate sub-concept, evaluate chain for fitness |
| `evolve-seed!` | C++ primitive | `( n -- )` | Seed all evolution RNGs for reproducibility |
| `evolve-chain` | TIL (builtins.til) | `( chain-str subs-array gens -- )` | Round-robin scheduling over sub-concepts |

Supporting changes:
- `EvolutionEngine::evolve_sub_concept()` — mutates one sub-concept's impls but evaluates the chain word's test cases for fitness, registering children under the sub-concept
- `EvolutionEngine::seed_rng()` — seeds `GeneticOps`, `ASTGeneticOps`, `BridgeMap`, and engine-level RNGs
- `Fitness` stdout capture — redirects `ExecutionContext::out_` to a buffer during fitness evaluation so mutated bytecode containing print words does not spam stdout
- `set_rng_seed()` added to `GeneticOps` and `ASTGeneticOps` headers
- Member `rng_` promoted from local variable in `evolve_word()` to `EvolutionEngine` member for seedable determinism

Tests: `test_mce.til` integration test (10 assertions, all passing). 1509/1509 tests pass.

---

## Benchmark Design

**Target function:** `f(x) = x^2 + 3x + 5` (Integer -> Integer)

**Monolithic** (`bench_mce_monolithic.til`):
- Single word `target-fn` evolved as a monolithic unit
- 30 generations of `evolve-word`

**MCE Chain** (`bench_mce_quad.til`):
- Decomposed: `square-term` / `linear-term` / `offset` / `target-fn`
- `target-fn = dup square-term swap linear-term + offset +`
- Tests registered on `target-fn`; sub-concepts evolved via `evolve-sub`
- 30 rounds of `evolve-chain` (= 90 `evolve-sub` calls: 30 x 3 sub-concepts)

**Test cases:** 9 inputs (x = -3, -1, 0, 1, 2, 3, 4, 5, 10)

**Fitness mode:** Distance-based (`evolve-fitness-mode 1`), alpha = 1.0

**Seeds:** 42, 123, 7, 999, 314159

Validation script: `tests/til/bench/validate_mce.sh`

---

## Results

### Fitness Comparison (30 generations, 5 seeds)

| Seed | Monolithic (passes / fitness) | MCE Chain (passes / fitness) |
|------|------|------|
| 42 | 9/9 f=0.999214 | 0/9 f=0.999403 |
| 123 | 9/9 f=0.999270 | 0/9 f=0.999381 |
| 7 | 9/9 f=0.999284 | 0/9 f=0.999400 |
| 999 | 9/9 f=0.999289 | 0/9 f=0.999381 |
| 314159 | 9/9 f=0.999292 | 0/9 f=0.999374 |

"Passes" = number of test cases where the evolved word produces an exact match.
"Fitness" = best fitness score observed in the evolution log across all generations.

### Observations

1. **Monolithic reaches perfect scores.** 9/9 exact matches consistently across all seeds. The evolution engine can find solutions to this benchmark.

2. **MCE fitness is comparable but without exact matches.** MCE chain fitness (~0.999) is on par with monolithic (~0.999), but 0/9 test cases produce exact matches. The high fitness comes from evaluating `target-fn` with the *original* correct sub-concept impls being selected by weighted-random. Children contribute to fitness only when they happen to be selected.

3. **No weight separation.** Children are pruned at similar weights (0.066–0.090). The pruning log shows no differentiation between impls that improve the chain and impls that degrade it — all cluster at similar weights because the chain fitness is dominated by the original impl.

4. **Implicit credit is noise.** When `evolve-sub` evaluates the chain, the child impl may or may not be selected by the dictionary's weighted-random selection engine. The probability of selection depends on relative weights. With the original impl holding most of the weight, the child is rarely selected, so its measured fitness reflects the chain with the *original* impl, not the child. Good and bad children receive nearly identical fitness scores.

---

## Evaluation Against Plan Criteria

The plan defined three pass criteria (at least one must hold):

### Criterion 1: Faster convergence to fitness >= 0.95

**FAIL.** Monolithic reaches 9/9 exact matches; MCE reaches 0/9. MCE does not converge faster.

### Criterion 2: Same final fitness with lower variance

**FAIL.** Final fitness is comparable (~0.999 vs ~0.999), but MCE achieves no exact matches. The fitness score is misleading — it reflects the original impls being selected, not the children.

### Criterion 3: Sub-concept weights cleanly separate good from bad impls

**FAIL.** All children cluster at similar weights. No separation between good mutations (that would improve the chain) and bad mutations (that would degrade it).

---

## Root Cause

The fundamental issue is that `evolve_sub_concept()` evaluates the chain word's impl, which calls the sub-concept through the dictionary. The dictionary uses weighted-random selection. The child is registered in the dictionary but competes with the original impl for selection. Since the original has high weight and the child starts with the chain's fitness (not its own contribution), the credit signal is diluted.

In concrete terms: when evaluating whether a mutated `square-term` is good, the engine runs `target-fn`. If `target-fn` calls the *original* `square-term` (which is likely), the fitness reflects the original, not the child. The child gets credit or blame for something it didn't do.

This is the same "anti-correlated credit" problem identified in the TBBP Validation Findings (`20260405C`): rewarding or penalizing based on outcomes the entity didn't influence.

---

## What Phase 1b Must Fix

Phase 1b's explicit credit mechanism must guarantee that the child is the one being evaluated. Two approaches:

**Option A: Forced selection.** Before evaluating the chain, temporarily set the child's weight to 1.0 and all other sub-concept impls to 0.0. This guarantees the child is selected during chain evaluation. Restore weights after.

**Option B: Direct substitution.** Instead of registering the child in the dictionary and hoping it's selected, temporarily replace the sub-concept's bytecode pointer during chain evaluation. This bypasses the selection engine entirely.

Option A is simpler (weight manipulation only). Option B is more invasive but deterministic.

The plan's default credit function (Option B: Survival reward) also applies: instead of using raw chain fitness as the child's weight, reward children that survive pruning over multiple rounds.

---

## Deliverables on This Branch

All Phase 1a deliverables are committed and tested:

- [x] `evolve-seed!` primitive (C++)
- [x] `evolve-sub` primitive (C++)
- [x] `EvolutionEngine::evolve_sub_concept()` method
- [x] `EvolutionEngine::seed_rng()` method
- [x] `evolve-chain` / `_evolve-round` TIL words (builtins.til)
- [x] Help entries for all new words
- [x] `test_mce.til` integration test (passing)
- [x] `bench_mce_quad.til` benchmark
- [x] `bench_mce_monolithic.til` benchmark
- [x] `validate_mce.sh` validation script
- [x] Fitness stdout capture fix
- [x] Benchmark README updated
- [x] 1509/1509 tests passing (debug + release)

The branch is held for Phase 1b (explicit credit), which will be implemented on this same branch per the plan.
