# Modular Co-Evolution (MCE) Phases 1-3 — Implementation Plan

**Date:** 2026-04-05
**Primary References:**
- `20260405A-MCE-Design-Review.md` — post-TDB re-assessment, revised phasing, sequencing rationale
- `20260405C-TBBP-Validation-Findings.md` — empirical signal analysis, reward-attribution lessons

**Secondary References:**
- `20260325A-Modular-Coevolution-Design.md` — original MCE design (2026-03-25)
- `20260405B-TBBP-Implementation-Plan.md` — TBBP plan format used as template
- `20260403B-Type-Directed-Bridges-Implementation-Plan.md` — TDB (shipped v1.12.10)

**Prerequisites:** v1.13.12 (TBBP shipped, BridgeMap with weighted selection operational, etil-web WASM deployed, 1486/1486 tests passing)
**Status:** Plan
**Workflow:** See `docs/claude-knowledge/20260403A-feature-branch-workflow.md` and `docs/claude-knowledge/20260404-speedbumps.md`

---

## Overview

The evolution engine currently evolves one monolithic `WordConcept` at a time. A chain like
`: target-fn square-term swap linear-term + offset + ;` can only be evolved by mutating
`target-fn` as a single AST. Working sub-concepts are not preserved across mutations.

**MCE makes sub-concepts first-class evolution targets.** The chain itself stays fixed; each
sub-concept has its own impl population that evolves under selection pressure from chain-level
fitness. The existing dictionary (multiple-impls-per-concept) + selection engine
(weighted-random) + per-concept evolution already provide ~40% of the infrastructure. What is
missing is **orchestration**: scheduling, credit accounting, and module-aware crossover.

This plan covers four phases:

| Phase | What | Where | Estimated Effort |
|---|---|---|---|
| **1a** | Round-robin evolve-chain TIL word | TIL-only (data/builtins.til) | ~1 day |
| **1b** | Chain-level credit accounting | C++ (EvolutionEngine) | ~3 days |
| **2**  | Co-evolutionary mean-over-partners credit | C++ (EvolutionEngine) | ~1 week |
| **3**  | Module-boundary crossover | C++ (ast_genetic_ops) | ~2 days |

Each phase ships as its own version bump on a feature branch, is benchmarked against the prior
phase's baseline, and is either merged or discarded based on a measurable delta. Phase 2 is
**conditional** — skip it if Phase 1b produces stable sub-concept weights.

**Ground truth benchmark for all four phases:** `tests/til/bench/bench_mce_quad.til`
(to be created in Phase 1a) — the quadratic regression `f(x) = x² + 3x + 5` decomposed as
`target-fn = square-term swap linear-term + offset +`. Every phase must beat monolithic
evolution of `target-fn` on this benchmark by a measurable margin or be reverted.

---

## Design Principles (Carry-Over from References)

Lessons drawn directly from `20260405A-MCE-Design-Review.md` and
`20260405C-TBBP-Validation-Findings.md` that shape this plan:

### From the MCE Design Review

1. **Implicit credit first, explicit later.** The weight system already credits winning
   sub-concept impls via chain fitness. Phase 1a uses implicit credit; Phase 1b adds explicit
   attribution only if Phase 1a's weights are too noisy.
2. **Global `BridgeMap` is the right abstraction.** All sub-concepts share one bridge
   vocabulary. MCE does not need per-sub-concept bridge maps.
3. **Error routing is load-bearing.** `Fitness::set_error_stream()` routes mutation errors to
   the evolution log. MCE's N× error volume demands this stays active.
4. **The poly-gap amplifies at depth.** Sub-concepts with `(Unknown → Unknown)` signatures
   (anything calling `+ - * /`) produce unbounded boundary types. Benchmarks should start with
   concrete-signature decompositions to isolate MCE gains from poly-gap confounds.
5. **Red Queen oscillation is possible but mitigated.** Round-robin scheduling holds N-1
   sub-concepts fixed while mutating the Nth — the same mitigation co-evolution literature uses.

### From the TBBP Validation Findings

1. **Fitness-delta reward can be anti-correlated.** TBBP penalized every bridge used during
   TypeRepair because TypeRepair only fires on broken mutations. **Lesson for MCE:** when
   designing credit-assignment rules (Phase 1b, Phase 2), verify empirically that the reward
   signal correlates with the thing being rewarded. Do not assume `child > parent ⇒ good`.
2. **"Survival reward" is more robust than fitness delta.** Path B from the validation
   findings — reward impls that survive pruning, penalize impls that get pruned — should be the
   default credit-assignment primitive for sub-concepts, not parent→child fitness delta.
3. **Probabilistic mutation tests are flaky.** Phase 1a/1b/2/3 tests must either fix the RNG
   seed or test invariants (e.g., *"chain fitness never decreases over N generations"*) rather
   than specific outcomes. Two tests were already disabled during TDB for this reason.
4. **Measure deltas against a declared baseline.** TBBP was shipped without a confirmed
   positive delta. MCE phases must each be gated on a measurable improvement over the prior
   phase's baseline on `bench_mce_quad.til`.

---

## Phase 1a — Orchestration Primitives (Minimal Viable MCE)

**Goal:** Demonstrate that round-robin evolution of user-decomposed sub-concepts converges on
the quadratic target faster (or at least no slower) than monolithic evolution. No C++ changes.

**Branch:** `scripts/branch.sh mce-phase-1a-orchestration` → v1.14.0
**Estimated effort:** ~1 day (TIL glue + benchmark)

### Design

MCE Phase 1a is a TIL orchestration pattern. The user decomposes the target into sub-concepts,
registers tests on the chain concept, and calls a round-robin `evolve-chain` word.

```til
# User decomposition
: square-term dup * ;                                   # (Integer -- Integer)
: linear-term dup 3 * ;                                 # (Integer -- Integer)
: offset 5 ;                                            # ( -- Integer)
: target-fn square-term swap linear-term + offset + ;   # (Integer -- Integer)

# Tests registered on the chain
s" target-fn" { 0 5  1 9  2 15  3 23  4 33 } evolve-register

# Round-robin evolution
s" target-fn" { s" square-term" s" linear-term" s" offset" } 100 evolve-chain
```

`evolve-chain` is defined in `data/builtins.til` (or a new `data/evolve.til`) as:

```til
# evolve-chain ( chain-str sub-concepts-array generations -- )
# Round-robin: N generations × each sub-concept
: evolve-chain
  ( chain-str sub-concepts-array generations )
  swap                               # chain-str generations sub-concepts-array
  >r 0 do                            # loop generations times
    r@ array-length 0 do             # loop over each sub-concept
      r@ i array-get                 # sub-concept string
      evolve-word drop               # evolve one generation of that sub-concept
    loop
  loop
  r> drop drop                       # discard sub-concepts-array and chain-str
;
```

**Key invariant:** every `evolve-word` call evaluates the chain (`target-fn`) to compute
fitness, but only mutates the named sub-concept. The selection engine picks current-best impls
of the other sub-concepts via weighted random.

### What Already Works

- `evolve-word "square-term"` already evaluates the chain through the dictionary (because
  `target-fn`'s bytecode resolves `square-term` at call time via weighted-random selection).
- Impls that participate in high-scoring chains accumulate weight via the existing
  `update_weights()` loop.
- The shared `BridgeMap` inserts bridges at chain boundaries automatically.

No C++ changes are required for Phase 1a. The only question is whether implicit credit
produces usable weight gradients.

### Implementation Checklist

- [ ] Add `evolve-chain` to `data/builtins.til` (or new `data/mce.til` loaded from bootstrap).
- [ ] Add `array-length` / `array-get` TIL words if they do not exist (verify first —
      they may already be in `array_primitives.cpp`).
- [ ] Create `tests/til/bench/bench_mce_quad.til`:
    - Decomposition: `square-term / linear-term / offset / target-fn`
    - Register tests on `target-fn` for `f(x) = x² + 3x + 5`
    - Run 100 rounds of `evolve-chain`
    - Print final best impl of each sub-concept and chain fitness
- [ ] Create `tests/til/bench/bench_mce_monolithic.til` as baseline:
    - Same target function, but as a single `target-fn` impl
    - Register tests, run 100 generations of monolithic `evolve-word`
    - Print final best fitness
- [ ] Add to `tests/til/bench/README.md`: how to run both benchmarks back-to-back.
- [ ] Record baseline: monolithic best fitness, convergence generation.

### Validation — What "Done" Looks Like

Phase 1a passes if **at least one** of the following holds across 5 runs with different seeds:

1. `bench_mce_quad.til` reaches fitness ≥ 0.95 in fewer generations than
   `bench_mce_monolithic.til` on the **same wall-clock budget**, OR
2. `bench_mce_quad.til` reaches the same final fitness as monolithic with lower variance across
   runs (module preservation claim), OR
3. Sub-concept weights cleanly separate "good" from "bad" impls (e.g., `square-term`'s
   `dup *` impl converges to weight ≥ 0.8 while `dup +` drops to ≤ 0.1).

Phase 1a fails if sub-concept weights oscillate randomly or all three metrics are worse than
monolithic.

### Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Implicit credit is too noisy — good sub-concept impls don't accumulate weight | Proceed to Phase 1b (explicit credit) |
| Round-robin order matters — some orderings converge, others don't | Benchmark with 3 orderings; if large delta, force round-robin to randomize order |
| `evolve-word` re-compiles sub-concept every generation, slowing benchmarks | Profile; if hot, cache compiled sub-concepts |
| Chain fitness dominated by one sub-concept (others don't matter) | Decomposition problem — try harder benchmark or accept that some chains are bottlenecked |

### Super-Push Gate

v1.14.0 ships only if Phase 1a validation passes (one of the three criteria above). If not, the
branch is held and Phase 1b is attempted directly on the same branch.

---

## Phase 1b — Chain-Level Credit Accounting

**Goal:** Give each sub-concept impl an explicit per-impl fitness contribution separate from
"whatever chain fitness happened when this impl ran." This tightens credit-assignment noise
that Phase 1a's implicit credit may expose.

**Branch:** `scripts/branch.sh mce-phase-1b-credit` → v1.15.0 (if 1a merged)
**Estimated effort:** ~3 days

### Design

Add a new concept type: **chain concept**. A chain concept declares its sub-concepts and their
expected stack effects. When `evolve-word` runs on a sub-concept, it evaluates the chain, but
the fitness gets routed through a **chain-aware credit function** rather than assigned raw to
the mutated impl.

New TIL word:
```til
# evolve-register-chain ( chain-str sub-concepts-array tests-array -- flag )
s" target-fn" { s" square-term" s" linear-term" s" offset" }
               { 0 5  1 9  2 15  3 23  4 33 }
               evolve-register-chain
```

The engine remembers that `target-fn` is a chain of three sub-concepts. When
`evolve-word "square-term"` runs:
1. Generate 5 new `square-term` impls via mutation.
2. For each new impl, evaluate `target-fn` over all test cases.
3. Compute chain fitness as before.
4. **New:** attribute fitness to `square-term` impl using a credit function (below).
5. Update `square-term` impl's weight using the attributed fitness, not raw chain fitness.

### Credit Function Options

Three candidate credit functions, each testable with an A/B ablation:

**Option A: Raw chain fitness (baseline = Phase 1a).**
```
credit(impl) = chain_fitness_when_impl_ran
```

**Option B: Survival reward (from TBBP Findings Path B).**
```
credit(impl) = 1.0 if impl survives pruning (weight ≥ threshold), 0.0 if pruned
```
Applied at the end of a round, not per-mutation. Survival-based signals are known to be more
robust than fitness-delta (see TBBP Validation Findings, Section "Path B").

**Option C: Marginal contribution via ablation.**
```
credit(impl) = chain_fitness_with_impl - chain_fitness_with_best_alternative
```
For each candidate impl, re-run the chain with the previous best impl, subtract. This is
`O(2×population)` per round — 2× slower but directly measures contribution.

**Default:** Option B (survival reward) with Option C available as an opt-in flag
(`evolve-credit-mode!`). Option B matches the "reward what survives" principle from biological
selection and avoids the fitness-delta anti-correlation that sank TBBP's signal.

### Implementation Checklist

- [ ] Add `ChainConcept` struct to `EvolutionEngine`: tracks sub-concepts + current impl
      selections.
- [ ] Add `prim_evolve_register_chain` + register in `primitives.cpp`.
- [ ] Add `EvolutionConfig::credit_mode` field: `Raw | Survival | Marginal` (default `Survival`).
- [ ] Add `prim_evolve_credit_mode` TIL word: `( mode-int -- )`.
- [ ] In `evolve_word()`: when the word is a sub-concept of a registered chain, route fitness
      through the credit function instead of the raw result.
- [ ] Add `EvolveLogCategory::ChainCredit` logging category for credit-function debugging.
- [ ] Unit tests:
    - `ChainCreditRawMatchesMonolithic` — Option A produces same weights as Phase 1a.
    - `ChainCreditSurvivalRewardsGoodImpls` — Option B: hand-craft 5 impls (4 good, 1 bad),
      verify bad one's weight → 0 after 3 rounds.
    - `ChainCreditMarginalIsolatesContribution` — Option C: construct a case where raw credit
      misattributes, verify marginal credit corrects it. **Fix RNG seed** per TBBP lesson.
- [ ] Add `bench_mce_quad_phase1b.til` variant: same as 1a but with `evolve-register-chain`
      + `evolve-credit-mode` flag.

### Validation

Phase 1b passes if, with Option B (Survival) as the credit mode, the following hold across
5 seeds on `bench_mce_quad.til`:

1. Sub-concept weights have **lower variance** than Phase 1a (measured as stddev over 5 runs
   of the final weight of the winning impl of each sub-concept).
2. Convergence generation for `target-fn` fitness ≥ 0.95 is **≤** Phase 1a's median.
3. No regression on Phase 1a's three original pass criteria.

Phase 1b fails if Option B produces worse credit separation than Option A. In that case, fall
back to Option A (Phase 1a behavior) and document the failure mode.

### Risks

| Risk | Mitigation |
|---|---|
| Option C (marginal) is too slow for large populations | Gate Option C behind opt-in flag; default to B |
| Survival threshold is benchmark-specific | Tune `prune_threshold` per-chain via `evolve-register-chain` options (future) |
| Chain concept vs word concept distinction bloats Dictionary API | Keep chains in `EvolutionEngine` only — don't touch `core::Dictionary` |

---

## Phase 2 — Co-Evolutionary Credit (Conditional)

**Goal:** Only attempt if Phase 1b's credit is still noisy. Evaluate each sub-concept impl
with multiple partner combinations and use the **mean fitness across partners** as the impl's
weight. This eliminates the Red Queen problem (impl A performs well only with partner B, so
B's replacement destroys A's apparent fitness).

**Branch:** `scripts/branch.sh mce-phase-2-coevol` → v1.16.0
**Estimated effort:** ~1 week
**Skip condition:** Phase 1b produces stable weights across 5 runs of
`bench_mce_quad.til` (stddev of winning-impl weight < 0.1 per sub-concept).

### Design

For each candidate impl of sub-concept S:
1. Pick K partner combinations from the current impl populations of the other sub-concepts.
2. Evaluate chain fitness for each combination.
3. Candidate's weight = **mean fitness across K combinations**.

K is a tuning parameter. Small K (e.g., 3) is fast but noisy. Large K (e.g., 10) is
thorough but expensive: `K × population × generations` fitness evaluations per phase.

**Complexity budget:** monolithic evolution runs `population × generations` evaluations per
sub-concept per phase. Phase 2 runs `K × population × generations`. For K=3 and 3 sub-concepts,
this is **9× the baseline cost**. For K=5, 15×. Budget must be validated empirically.

### Partner Selection Strategies

1. **Current-best partners (fastest).** Use each partner sub-concept's top-weighted impl.
   K=1, deterministic, but collapses to Phase 1b behavior.
2. **Weighted-random partners (default).** Sample K partner tuples from the selection engine's
   weighted distribution. Preserves co-evolutionary diversity.
3. **All-pairs (exhaustive).** Enumerate all partner combinations. K = product of partner
   population sizes. Only viable for tiny populations (<5 impls each).

Default: weighted-random with K=3.

### Implementation Checklist

- [ ] Add `EvolutionConfig::coevol_partners` (K) and `EvolutionConfig::coevol_strategy` fields.
- [ ] Add `prim_evolve_coevol_k` and `prim_evolve_coevol_strategy` TIL words.
- [ ] Extend `evolve_word()` to loop K times when the word is a chain sub-concept AND
      `coevol_partners > 1`. Compute mean fitness.
- [ ] Track partner-combination variance per candidate for logging.
- [ ] Add `bench_mce_quad_phase2.til` variant: same benchmark, but with K=3 partners.
- [ ] Unit tests: construct a case with two partner-dependent impls, verify Phase 1b
      misattributes and Phase 2 corrects. Fix RNG seed.

### Validation

Phase 2 passes if **all** of the following hold:

1. Sub-concept weight variance is **lower** than Phase 1b across 5 seeds.
2. Wall-clock to fitness ≥ 0.95 is **≤ K×** Phase 1b (i.e., the per-partner cost is amortized).
3. At least one pathological case exists where Phase 2 solves a convergence failure that 1b
   cannot — documented in `docs/claude-knowledge/` as a validated MCE use case.

If criterion 3 cannot be constructed, **the phase is discarded**. Phase 1b is kept, Phase 2
branch is held as future work.

### Risks

| Risk | Mitigation |
|---|---|
| K× cost kills benchmarks | Keep K ≤ 3 by default; gate larger K behind explicit flag |
| Partner selection favors converged impls, killing exploration | Use weighted-random (strategy 2) with a small epsilon |
| Mean fitness masks partner-specific brilliance | Log variance alongside mean; surface to `evolve-log` |

---

## Phase 3 — Module-Boundary Crossover

**Goal:** Add `chain_crossover` — swap sub-concept impl references between two chains — as a
complement to word-level AST crossover. This is "sexual reproduction at the module level."

**Branch:** `scripts/branch.sh mce-phase-3-crossover` → v1.17.0 (or v1.16.0 if Phase 2 skipped)
**Estimated effort:** ~2 days

### Design

The existing `block_crossover` in `ast_genetic_ops.cpp:240` swaps AST subtrees between two
parent programs. It is token-aware but not module-aware. `chain_crossover` takes two chains
(parents), builds a child that inherits one parent's impl selection for some sub-concepts and
the other parent's selection for the rest.

```
Parent A: square-term#4  + linear-term#7  + offset#2   → fitness 0.6
Parent B: square-term#9  + linear-term#3  + offset#5   → fitness 0.4

Crossover (random mask 101):
  Child:  square-term#4  + linear-term#3  + offset#2   → fitness ???
```

Because each sub-concept has a defined stack effect, module-boundary swaps never produce
structurally invalid chains. No type repair needed at the crossover point (though TDB's
bridges may still be invoked if sub-concept output/input types mismatch — which is an
*existing* type-safety concern, not a new one).

**Integration with Phase 1b/2:** crossover produces a new chain-impl-selection tuple. Fitness
is evaluated on the tuple. Credit is attributed per the current credit mode.

### Implementation Checklist

- [ ] Add `ChainCrossover` operation to `ASTGeneticOps` (or a new `ChainGeneticOps` class if
      the responsibility split is cleaner).
- [ ] Add `MutationWeights::chain_crossover` (default 0.1; other weights renormalized).
- [ ] Extend `evolve_word()` for chain sub-concepts: with probability `chain_crossover_rate`,
      perform a chain crossover instead of word-level mutation.
- [ ] Unit tests:
    - `ChainCrossoverPreservesSubConceptValidity` — produced child calls exist in dictionary.
    - `ChainCrossoverCombinesParentTraits` — hand-construct parents, verify mask applied.
    - `ChainCrossoverFitnessDistribution` — N crossovers produce a mix of fitnesses spanning
      parent range. Fix RNG seed or test invariants only.
- [ ] `bench_mce_quad_phase3.til`: same benchmark with crossover enabled; expect faster
      convergence than Phase 1b/2 alone.

### Validation

Phase 3 passes if `bench_mce_quad.til` converges in **fewer generations** than the previous
phase's baseline across 5 seeds, with no fitness regression.

Phase 3 fails if chain crossover produces children that are strictly worse than pure mutation
(i.e., the crossover rate of 0.1 should not HURT convergence — if it does, the genetic operator
is not adding signal).

### Risks

| Risk | Mitigation |
|---|---|
| Crossover + survival credit double-counts good impls | Ensure credit attribution runs on child, not parent |
| 3-sub-concept chains produce only 2^3=8 crossover children; quickly exhausted | Crossover rate should remain low (0.1); mutation dominates |
| Crossover children inherit type-mismatched sub-concepts | Existing TDB bridges at chain boundaries handle this |

---

## Shared Infrastructure & Cross-Cutting Concerns

### Benchmark Suite

All four phases add to `tests/til/bench/`:
- `bench_mce_monolithic.til` — baseline (Phase 1a)
- `bench_mce_quad.til` — round-robin MCE (Phase 1a-3 all reuse this, with escalating
  `evolve-register-chain` / `evolve-credit-mode` / `evolve-coevol-k` options)

Each phase's validation requires **5 seed runs** to smooth out probabilistic variance. The
benchmark README gets a new section: "MCE Phase Comparison" with columns for Gen to 0.95,
Final Fitness, Weight Variance per sub-concept.

### RNG Seed Control

Per the TBBP Validation Findings lesson: probabilistic mutation tests must fix the RNG seed
OR test invariants only. The MCE phases add a new word if one does not already exist:
```til
42 evolve-seed!   # fix RNG seed to 42 for reproducible benchmarks
```
If `evolve-seed!` does not exist today, add it as part of Phase 1a infrastructure.

### Logging

Add `EvolveLogCategory::ChainCredit` to `evolve_logger.hpp` for Phase 1b+ debugging. Log
per-candidate credit attribution at `EvolveLogLevel::Debug`.

### Error Routing

No change — `Fitness::set_error_stream()` already routes mutation errors to the evolution log.
MCE's N× error volume makes this non-negotiable.

### Poly-Gap Mitigation

The benchmarks must use sub-concepts with **concrete type signatures** to isolate MCE gains
from poly-gap confounds. For `bench_mce_quad.til`:
- `square-term` must be typed `(Integer → Integer)` via manual annotation or an empirically-
  refined signature (Open Question 6 from the MCE Design Review).
- `linear-term` likewise.
- `offset` is `( → Integer)`.

If sub-concepts come out as `(Unknown → Unknown)`, the benchmark is invalid for testing MCE
vs monolithic — the type fuzziness dominates.

**Action item for Phase 1a:** add manual `evolve-annotate-signature` TIL word OR document the
requirement that benchmark sub-concepts must have explicit concrete-type bodies.

---

## Revisiting TBBP After MCE

The TBBP Validation Findings (`20260405C-TBBP-Validation-Findings.md` §"Recommendation") noted:
> Revisit with Path B or Path C as a v1.14+ experiment once MCE is operational. MCE's
> round-robin evolution may produce cleaner fitness deltas that could make TBBP's signal more
> actionable.

After Phase 1b ships, re-run TBBP's `bench_cross_domain.til` with `evolve-tbbp-enabled?` ON
inside an MCE chain. Hypothesis: round-robin evolution's cleaner per-sub-concept fitness
deltas will produce a non-zero TBBP reward signal where monolithic evolution did not.

This is **not part of Phases 1-3**. It is a follow-up validation experiment recorded here so
it is not lost.

---

## Sequencing Summary

```
v1.13.12 (master, current)
    │
    ├── Phase 1a branch: mce-phase-1a-orchestration
    │     └── v1.14.0 — TIL evolve-chain + bench_mce_quad.til
    │         └── [validation gate: 1-of-3 criteria]
    │             └── merge to master
    │
    ├── Phase 1b branch: mce-phase-1b-credit
    │     └── v1.15.0 — chain concept + survival credit
    │         └── [validation gate: variance ↓, convergence ≤]
    │             └── merge to master
    │
    ├── Phase 2 branch: mce-phase-2-coevol  [CONDITIONAL]
    │     └── v1.16.0 — mean-over-partners credit
    │         └── [validation gate: solves pathological case]
    │             └── merge OR hold branch as future work
    │
    └── Phase 3 branch: mce-phase-3-crossover
          └── v1.17.0 (or v1.16.0 if 2 skipped) — chain_crossover
              └── [validation gate: faster convergence]
                  └── merge to master
```

After each merge, update `docs/claude-knowledge/20260405B-Context-Bootstrap-TBBP-MCE.md` with
the new state (phases shipped, benchmarks validated, deltas observed).

---

## Open Questions

1. **Chain type repair: per-sub-concept or full-chain?** The MCE Design Review flagged this as
   silent in the original design. This plan defers: Phase 1a runs type repair per sub-concept
   (today's behavior). If chain boundaries expose type mismatches, revisit in Phase 1b.

2. **Empirical signature refinement (MCE Design Review Open Question 6).** Can `square-term`'s
   signature be refined from `(Unknown → Unknown)` to `(Integer → Integer)` from runtime test
   observations? This is orthogonal to MCE Phases 1-3 but mentioned in the MCE Design Review
   as a poly-gap mitigation. **Deferred** — not addressed in this plan.

3. **Chain decomposition discovery (Phase 4).** How does the engine discover sub-concept
   boundaries on its own? Requires gene duplication from the Representation Experiments plan.
   **Deferred** — not addressed in this plan.

4. **Fitness evaluation reuse across sub-concepts.** When round-robin evolution runs
   `square-term` then `linear-term` in back-to-back generations on identical partner impls,
   could cached chain fitness be reused? Potential optimization; evaluate only if Phase 2's
   K× cost becomes a bottleneck.

5. **TBBP × MCE interaction.** Does weighted bridge selection (TBBP) produce different results
   inside MCE's round-robin than in monolithic evolution? Addressed by the "Revisiting TBBP"
   follow-up experiment.

---

## What This Plan Does NOT Cover

- **Phase 4** (evolved chain structure — add/remove sub-concept calls). Requires gene
  duplication infrastructure. Deferred.
- **Nested MCE** (Phase 5 — tree-structured sub-concept hierarchies). Per the MCE Design Review,
  this is deferred until linear MCE (Phases 1-3) is validated.
- **Cross-session weight persistence.** MCE weights reset per session, mirroring TDB/TBBP.
- **UI/visualization of chain evolution.** The `etil-tui` project handles observation; MCE
  ships with log output only.

---

## Summary

MCE Phases 1-3 convert ETIL's monolithic-concept evolution into chain-level co-evolution,
reusing ~40% of existing infrastructure (dictionary, selection engine, per-concept evolution,
bridge map). Each phase ships as an independent version bump with an empirical validation gate
measured on the quadratic regression benchmark. Phase 2 is conditional. Phase 3 adds
module-boundary sexual reproduction. Lessons from TBBP's weak-signal shipment shape this
plan's insistence on (1) survival-based credit over fitness-delta, (2) RNG-seed fixed tests,
(3) explicit validation gates per phase, and (4) concrete-signature benchmarks to isolate MCE
from the poly-gap.

The original MCE design held up. TDB made it affordable. TBBP's shipment taught the team to
measure before merging.
