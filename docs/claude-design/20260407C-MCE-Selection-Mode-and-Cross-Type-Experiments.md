# MCE Selection Mode and Cross-Type Benchmark Experiments

**Date:** 2026-04-07
**Branch:** `master` (v2.2.2 + uncommitted experiment infrastructure)
**Predecessors:**
- `20260407A-MCE-Phase-1a-Validation-Results.md`
- `20260407B-MCE-Phase-1b-Validation-Results.md`

---

## Motivation

Phase 1b fixed the dictionary cache bug, making MCE functional. But the Phase 1b validation raised two questions:

1. **Selection mode matters.** The fitness evaluator uses `Dictionary::lookup()` (returns latest impl) because no `SelectionEngine` is set on the evaluation context. At runtime, the interpreter uses `Dictionary::select()` with weighted-random. These produce different results. How much does the selection mode affect MCE outcomes?

2. **Type homogeneity.** The quadratic benchmark decomposes `f(x) = x² + 3x + 5` into three sub-concepts that are all Integer→Integer. The mutation search space is identical for each. A cross-type benchmark where sub-concepts span different type domains should better exercise MCE's modular advantage.

---

## Experiment Design

### Independent Variables

**Selection mode** (3 levels):
- **Monolithic** — `evolve-word` on the chain word directly. Baseline.
- **MCE-lookup** — `evolve-sub` with `Fitness::sel_engine_ = nullptr`. The `Call` instruction resolves via `Dictionary::lookup()` which returns the latest registered impl. After `register_word(child)`, the child is the latest and is deterministically selected.
- **MCE-weighted** — `evolve-sub` with `Fitness::sel_engine_` set to a `WeightedRandom` selection engine. The `Call` instruction resolves via `Dictionary::select()`, so the child competes with all other impls based on weight. This matches runtime behavior.

**Benchmark** (2 levels):
- **quad** — `f(x) = x² + 3x + 5`. Decomposition: `square-term` (dup \*), `linear-term` (3 \*), `offset` (5). All sub-concepts are Integer→Integer. 30 generations.
- **xtype** — `f(x) = slength(number->string(x² + 1))`. Decomposition: `compute` (dup \* 1+, Integer→Integer), `format` (number->string, Integer→String), `measure` (slength, String→Integer). Sub-concepts span three type domains. 50 generations.

**Seed** — 5 seeds: 42, 123, 7, 999, 314159.

### Implementation

Added `evolve-mce-select ( flag -- )` primitive to toggle the fitness evaluator's selection engine at runtime. When true, `Fitness::run_single_test()` sets `ctx.set_selection_engine()` on the evaluation context, causing `Call` instructions to use weighted-random selection instead of latest-impl lookup.

Validation script: `tests/til/bench/validate_mce.sh`

---

## Results

### Quad Benchmark (Integer→Integer homogeneous, 30 gens)

| Seed | Monolithic | MCE-lookup | MCE-weighted |
|------|-----------|------------|--------------|
| 42 | 9/9 f=0.999 | 9/9 f=0.999 | 6/9 f=0.697 |
| 123 | 9/9 f=0.999 | 6/9 f=0.444 | 6/9 f=0.768 |
| 7 | 9/9 f=0.999 | 9/9 f=0.428 | 5/9 f=0.600 |
| 999 | 2/9 f=0.428 | 1/9 f=0.336 | 5/9 f=0.597 |
| 314159 | 9/9 f=0.999 | 2/9 f=0.401 | 6/9 f=0.750 |

### Xtype Benchmark (Integer→String→Integer cross-type, 50 gens)

| Seed | Monolithic | MCE-lookup | MCE-weighted |
|------|-----------|------------|--------------|
| 42 | 11/11 f=0.999 | 11/11 f=0.998 | 7/11 f=0.670 |
| 123 | 11/11 f=0.999 | 11/11 f=0.999 | 7/11 f=0.806 |
| 7 | 11/11 f=0.999 | 0/11 f=0.108 | 6/11 f=0.592 |
| 999 | 11/11 f=0.999 | 3/11 f=0.604 | 6/11 f=0.711 |
| 314159 | 11/11 f=0.999 | 1/11 f=0.461 | 6/11 f=0.589 |

---

## Analysis

### 1. Monolithic dominates both benchmarks

Monolithic evolution reaches perfect scores on nearly all seeds (9/9 quad, 11/11 xtype). The exception is seed 999 on quad (2/9), which shows monolithic is not immune to bad mutation sequences, but it's the outlier.

Monolithic wins because it evolves the full chain as a single unit. A mutation that changes one instruction is evaluated in the context of the complete, working chain. Good mutations preserve the chain's overall structure; bad mutations are immediately penalized.

### 2. MCE-lookup has high variance

MCE-lookup produces the sharpest signal (deterministic child selection) but the most inconsistent results:
- Quad: 1/9 to 9/9 across seeds
- Xtype: 0/11 to 11/11 across seeds

The determinism is double-edged. When the child IS the one being evaluated, a bad mutation gets a definitively bad score (correct behavior). But because the child is always selected regardless of weight, early bad mutations can cascade: a degraded `square-term` is always used when evaluating `linear-term` mutations in the next round, so partner quality is non-deterministic.

### 3. MCE-weighted is more consistent but caps lower

MCE-weighted produces narrower ranges:
- Quad: 5/9 to 6/9
- Xtype: 6/11 to 7/11

The weighted-random selection blends the child with existing impls. When the child is bad, it may not be selected, and the evaluation reflects the original (good) impl. When the child is good, it may not be selected either. The signal is diluted but stable.

The consistency comes at a cost: MCE-weighted never reaches perfect scores. The blended evaluation can't distinguish between "child is perfect" and "child wasn't selected and the original produced the right answer."

### 4. Cross-type benchmark did not differentiate

The xtype benchmark was designed to give MCE an advantage by placing sub-concepts in different type domains (Integer, String), so each sub-concept's mutation search space would be distinct. In practice, the results are comparable to quad.

The reason: the sub-concepts are still too simple. `compute` is 3 instructions (`dup * 1+`), `format` is 1 instruction (`number->string`), `measure` is 1 instruction (`slength`). Type heterogeneity helps when there are many candidate words per type domain, but a 1-instruction sub-concept has almost no room for beneficial mutation. Any substitution produces a wrong answer.

### 5. The fundamental MCE challenge on this class of benchmarks

MCE's theoretical advantage is modular search: evolving `square-term` independently explores only the Integer→Integer arithmetic space, while monolithic evolution of the full chain might waste mutations on string or type-conversion words that can't help.

But this advantage requires sub-concepts with **search depth** — enough internal complexity that the mutation operators can explore meaningfully. A 2-instruction sub-concept like `dup *` has no search depth. A 20-instruction sub-concept that implements a sorting algorithm would.

The current benchmarks test MCE infrastructure, not MCE advantage. A benchmark that demonstrates MCE's value would need:
- Sub-concepts with 10+ instructions each
- Initial implementations that are suboptimal (room for improvement)
- A decomposition where improving one sub-concept doesn't require simultaneously changing another

---

## Selection Mode Implications for Phase 2

Phase 2 proposes evaluating each child with K partner combinations (mean-over-partners). The results suggest:

- **MCE-lookup is the wrong default.** Its high variance means any single evaluation is unreliable. Phase 2's K-fold evaluation would help, but the cost is K× evaluations.
- **MCE-weighted is closer to runtime behavior** but dilutes the signal too much for credit assignment.
- **Forced selection with K=1** (current MCE-lookup) is the right primitive. Phase 2 should build on it by evaluating K times with different forced partners, not by switching to weighted-random.

The correct Phase 2 design: for each child of sub-concept S, force the child for S, force a randomly-sampled partner impl for each other sub-concept, evaluate, repeat K times, take the mean. This combines MCE-lookup's sharp signal with Phase 2's partner diversity.

---

## Files

| File | Purpose |
|------|---------|
| `evolve-mce-select` primitive | Toggle fitness selection engine |
| `/tmp/mce_mono.til` | Monolithic quad benchmark |
| `/tmp/mce_chain.til` | MCE-lookup quad benchmark |
| `/tmp/mce_chain_wr.til` | MCE-weighted quad benchmark |
| `/tmp/mce_mono_xtype.til` | Monolithic xtype benchmark |
| `/tmp/mce_chain_xtype.til` | MCE-lookup xtype benchmark |
| `/tmp/mce_chain_xtype_wr.til` | MCE-weighted xtype benchmark |
| `tests/til/bench/validate_mce.sh` | Validation runner |
