# ConceptDAG — Phase P1 Contribution Weight Validation

**Date:** 2026-04-14
**Version:** P1 → v2.3.11
**Plan:** `20260413B-ConceptDAG-Tier-B-Implementation-Plan.md`, prerequisite P1
**Benchmark:** `20260413C-ConceptDAG-Tier-B-Benchmark-Design.md` (integer norm)
**Status:** Branch A resolved (with caveats documented below)

---

## Purpose

P0 produced a benchmark with a structural asymmetry designed to stress
the Phase 2 contribution-weighting signal: three trivial utility
sub-concepts (`sq`, `sum3`) and one 15-instruction heavy lifter
(`sqrt-approx`). P1's job was to measure whether contribution weights
actually differentiate sub-concepts on this benchmark, and — if not —
to redesign the signal following Branch B.1 (fitness-delta tracking)
or Branch B.2 (UCB-style exploration).

This doc records the measurement, the three signal iterations that
were attempted, the bug that turned a "no" result into a "yes" result,
and the final resolution.

---

## Initial measurement (the signal is flat on unchanged v2.3.10)

Ran `bench_dag_tierb.til` at 100 generations across all 5 seeds
(42, 123, 7, 999, 314159) with the v2.3.10 code unchanged and parsed
contribution weights from the `evolve-dag-show` output.

| Seed | sqrt-approx | sum3 | sq |
|------|-------------|------|-----|
| 42 | 0.333 | 0.333 | 0.333 |
| 123 | 0.333 | 0.333 | 0.333 |
| 7 | 0.333 | 0.333 | 0.333 |
| 999 | 0.333 | 0.333 | 0.333 |
| 314159 | 0.333 | 0.333 | 0.333 |

Every concept on every seed normalized to exactly `1/N = 0.333`.
The per-concept variance was `0.0000` in the display (essentially zero
at the precision printed). **No differentiation whatsoever.** Branch B
confirmed.

The root cause was the same as on the quadratic benchmark: the v2.3.10
variance signal computed the sample variance of impl weights in the
dictionary. Within a few generations, all surviving impls for every
concept clustered near the prune threshold (most mutations are
catastrophic, so fitness collapses to `prune_threshold` for everything),
and the variance degenerated to ~0 uniformly. Pruning exacerbates this
by filtering out the one or two outlier high-fitness impls.

---

## Iteration 1 — Fitness-history variance (Branch B.1 first attempt)

**Signal:** for each concept, accumulate running variance of chain
fitness across every child evaluation in `evolve_sub_concept`, using
`ConceptNodeStats::record_fitness` (Welford's online algorithm).
Variance becomes the contribution weight.

**Reasoning:** this sidesteps the pruning issue by tracking the full
history of observations rather than just the current impl population.
A concept whose mutations produce a wide range of chain fitness
outcomes should have higher variance than one whose mutations all
produce similar (broken) fitness.

**Implementation:**

- Added `record_fitness(fr.fitness)` calls in `evolve_sub_concept`'s
  baseline loop and child loop, pointing at `dags_[chain_word].node(sub_concept)`.
- Replaced the impl-weight-variance computation in `evolve_dag_generation`
  with a read of `cn->stats.fitness_variance / (eval_count - 1)`.
- `DAG::reset()` already clears stats on `register_dag`, so the signal
  starts fresh per run.

**Result:** variance still collapsed to ~0 on all seeds at 100
generations. The display showed `var=0.0000` and contributions of
`0.333` uniformly.

**Diagnosis (via 5-gen early-run experiment):** at 5 generations the
signal *did* differentiate — `sqrt-approx` had `contrib=0.999,
var=0.0022` while `sq` and `sum3` had floor values. But at 100
generations the signal degraded because chain fitness converges
toward a "broken" plateau as broken mutations accumulate: with
latest-wins dictionary lookup, the first bad mutation of any concept
poisons the chain for the rest of the run. Every subsequent child
evaluation — regardless of which concept is being mutated — scores
around the same `~0.07–0.09` range (distance-based fitness of a
uniformly-broken chain). Variance stabilizes at a similar small value
for every concept, and normalization brings contributions back to
uniform `1/N`.

So history-variance differentiates briefly and then decays. Not
sustainable.

---

## Iteration 2 — Peak chain fitness as signal

**Signal:** `contribution = best_fitness + 1e-6` (normalized). Use the
maximum chain fitness ever observed while evolving this concept. This
is monotonic — it only goes up, never decays. A concept whose mutation
space can produce a correct (or near-correct) chain gets a high signal;
a concept whose mutations always flat-line at the prune floor gets a
low signal.

**Reasoning:** `best_fitness` is already tracked by
`ConceptNodeStats::record_fitness`. Switching the signal from
running variance to running max costs nothing extra. And unlike
variance, max never degrades as observations accumulate.

**Result:** winner-take-all. Once any concept found a good mutation
(e.g. `sq` achieved `best=0.986` in gen 1), its contribution jumped
to `0.999` and dominated all subsequent selection. The other concepts
(`sum3`, `sqrt-approx`) stayed at the floor `1e-6` and **never got
evolved at all** — their `eval_count` remained 0 for the entire run.

This was clearly visible in the final dump: one concept had
`gens=1448` (effectively all the budget) while the others had `gens=0`.

**Diagnosis:** `best_fitness + floor` produces unbounded
differentiation with no exploration term. It is the exact opposite
failure mode from Iteration 1: Iteration 1 differentiated nothing;
Iteration 2 differentiated too aggressively and killed exploration.

Also tripped over a debugging-infrastructure mistake: I was running
the *release* build (`build/bin/etil_repl`, stale at 12:35) while
rebuilding the *debug* build (`build-debug/bin/etil_repl`). The
release binary didn't have my P1 changes at all, so for several
iterations I was comparing v2.3.10 output against itself and
concluding (wrongly) that the new signal wasn't working. This cost
about an hour of diagnostic effort before I noticed the binary
mtime mismatch.

Lesson for Tier B: `validate_mce.sh` and all diagnostic scripts
should parameterize the REPL binary and default to release, and I
should always check binary mtime after a build.

---

## Iteration 3 — UCB-style exploit + explore (final)

**Signal:**

```
total_gens = Σ generations_evolved across all evolvable concepts
ln_total   = ln(total_gens + e)
contribution_i = best_fitness_i + c * sqrt(ln_total / (1 + gens_i)) + 1e-6
                   └── exploit ──┘  └────────── explore ─────────┘
```

with `c = 0.5`.

**Reasoning:** this is UCB1 adapted for the contribution-weighting
context. The **exploit** term is the same as Iteration 2's best-fitness
signal — it captures "upper bound on what this concept's mutation space
produces." The **explore** term adds a bonus that:

- is **large** for untried concepts (`gens_i = 0` → bonus ≈ `c * sqrt(ln_total)`),
  so every concept gets scheduled promptly at least once.
- **shrinks** as a concept accumulates generations
  (`1/sqrt(1 + gens_i)`), so once a concept has been explored enough,
  its exploit term dominates.
- **grows slowly** with total evolution
  (`sqrt(ln_total)`), so the exploration pressure stays present even
  late in the run.

`c = 0.5` chosen empirically: large enough that under-explored
concepts always stay in rotation (no starvation); small enough that
the differentiation between "concept that found a 1.0" and "concept
stuck at 0.1" remains meaningful.

---

## P1 results — Iteration 3 validated

Ran `bench_dag_tierb.til` at 100 generations across all 5 seeds in
release mode:

| Seed | sq (contrib / best) | sum3 (contrib / best) | sqrt-approx (contrib / best) |
|------|---------------------|-----------------------|------------------------------|
| 42 | **0.624** / 1.000 | 0.198 / 0.100 | 0.178 / 0.100 |
| 123 | 0.373 / 0.801 | 0.186 / 0.261 | **0.441** / 1.000 |
| 7 | **0.626** / 1.000 | 0.175 / 0.102 | 0.199 / 0.101 |
| 999 | 0.368 / 0.750 | **0.446** / 1.000 | 0.187 / 0.201 |
| 314159 | **0.551** / 1.000 | 0.211 / 0.217 | 0.238 / 0.300 |

**Observations:**

1. **Differentiation is clear and consistent.** On every seed, the
   "winner" (the concept that happened to find a near-perfect mutation)
   has 2–4× the contribution of the lowest-weighted concept. No seed
   converges to uniform `1/N`.

2. **The winning concept varies by seed.** `sq` wins 3/5 (seeds
   42, 7, 314159), `sum3` wins 1/5 (999), `sqrt-approx` wins 1/5 (123).
   This is surprising given the original hypothesis that the
   15-instruction `sqrt-approx` would dominate because of its larger
   mutation space.

3. **best_fitness reaches 1.0 for the winner on 4/5 seeds.** The
   winning concept on those seeds found a child whose chain evaluation
   passed all test cases. Seed 999 almost-won with `best=0.750`.

4. **UCB exploration prevents starvation.** Every concept gets at
   least 14 generations on every seed (min observed: sqrt-approx 14
   gens on seed 999). No concept is abandoned entirely.

5. **Contribution tracks scheduling.** Winners get 49–62 generations;
   losers get 14–24. The scheduler is acting on the signal, not just
   displaying it.

**Branch A satisfied:** contribution weights differentiate sub-concepts
meaningfully (strongest > weakest by at least 2×, often >3×). P1's
exit criterion is met. Tier B can proceed to Phase 5a.

---

## Surprising finding: the heavy lifter isn't the winner

My benchmark design predicted `sqrt-approx` (15 instructions) would
dominate because it has the largest mutation space and should produce
the highest variance of fitness outcomes. **Empirically, it doesn't.**
Across 5 seeds, `sqrt-approx` wins only once.

Why: a 15-instruction word has 15 opportunities to break completely
with each mutation. A 2-instruction word (`sq`, `sum3`) is almost
always *either* correct *or* completely broken — there's no middle
ground. Counterintuitively, this produces MORE high-fitness children
for small words than for large ones, because mutations of small words
either collapse fitness to ~0.1 or keep it at ~1.0, while mutations
of large words produce a broader distribution of partial-failure
fitnesses in the 0.1–0.3 range.

So "instruction count" is not a proxy for "mutation impact." The
Phase 2 signal correctly measures the latter, and that turns out to
favor small concepts whose mutations can accidentally still be correct.

This is useful information for Phase 5a onwards. Topology mutations
like `insert_node` (which adds a new concept between a parent and a
child) may benefit simple parents more than complex ones, because
insertion around a complex concept is more likely to break it. We
should design Phase 5a's benchmark with this in mind.

---

## 3-way comparison after P1 changes

With UCB contribution weighting and `evolve-mce-select true` active,
running `validate_mce.sh tierb` at 5 seeds:

| Seed | Monolithic | MCE-chain | MCE-DAG |
|------|-----------|-----------|---------|
| 42 | 9/9 f=0.999 | 1/9 f=0.30 | 2/9 f=0.30 |
| 123 | 9/9 f=0.999 | 9/9 f=0.999 | 9/9 f=0.999 |
| 7 | 9/9 f=0.999 | 3/9 f=0.68 | 2/9 f=0.31 |
| 999 | 9/9 f=0.999 | 1/9 f=0.34 | 7/9 f=0.80 |
| 314159 | 9/9 f=0.999 | 1/9 f=0.35 | 4/9 f=0.60 |

Success rate (runs with at least one 9/9 child):

| Strategy | Tier B P0 (uniform) | Tier B P1 (UCB) |
|----------|---------------------|-----------------|
| Monolithic | 5/5 | 5/5 |
| MCE-chain | 1/5 | 1/5 |
| MCE-DAG | 4/5 | 1/5 |

**DAG success rate dropped from 4/5 to 1/5** compared to P0's uniform-
scheduling result. This is a significant regression in the "find a
9/9 child" metric even though the signal itself is doing its job.

**Caveat:** the comparison is not strictly apples-to-apples because
the DAG benchmark enables `evolve-mce-select true` (weighted-random
selection during fitness eval) while the chain and monolithic
benchmarks do not. MCE-chain still uses latest-wins dictionary lookup
during chain evaluation, which behaves very differently from
weighted-random. Comparing pass counts directly between the two is
only meaningful if both use the same selection mode.

**Hypothesis on why UCB-DAG is worse despite correct signal:** by
focusing exploration on the concept that found an early good child,
UCB reduces the budget available for the other concepts to discover
their own good children. The P0 uniform scheduling had the opposite
problem (flat scheduling couldn't prioritize at all) but at least
gave every concept enough generations to occasionally find a good
mutation. Overall, *consistent moderate exploration* may matter more
for this benchmark than *intelligent exploitation*, because the
fitness landscape is so spiky (most mutations are catastrophic, a
few are nearly perfect).

**This is worth investigating in Phase 5a**, but it does not block P1:
the signal *differentiates correctly*, which was the required exit
criterion. Whether that differentiation *helps* evolution is a
separate question that depends on the benchmark's fitness landscape
and the exploration/exploitation balance. A tunable `c_explore`
constant (currently hardcoded to 0.5) would let us dial the tradeoff.

---

## What changed in v2.3.11

**`src/evolution/evolution_engine.cpp`:**

- `evolve_sub_concept` now looks up the DAG node for `sub_concept`
  at the top of the function (if `chain_word`'s DAG is registered)
  and calls `concept_node->stats.record_fitness(fr.fitness)` for
  every chain fitness observation in both the baseline loop and the
  child loop. This feeds chain-level outcomes into Welford's running
  stats per concept.

- `evolve_dag_generation` now computes contribution weights via
  UCB-style exploit + explore:
  ```cpp
  size_t total_gens = Σ generations_evolved;
  double ln_total = log(total_gens + e);
  for each concept:
      exploit = stats.best_fitness;
      explore = c_explore * sqrt(ln_total / (1 + gens));
      contribution = exploit + explore + 1e-6;
  normalize_contributions();
  ```
  with `c_explore = 0.5` hardcoded. The impl-weight-variance
  computation from v2.3.10 is removed.

**`tests/til/bench/bench_dag_tierb.til`:**

- Added `true evolve-mce-select` before `evolve-dag-register`. This
  enables weighted-random selection during fitness evaluation in the
  DAG-registered chain, without which the chain-fitness signal
  degrades to a "permanently broken" plateau after the first
  catastrophic mutation and nothing the scheduler does can differentiate.

---

## Deferred items

- **Make `c_explore` a TIL-settable parameter.** Currently hardcoded
  to 0.5. Add `evolve-dag-explore! ( x -- )` and a matching query
  word, so benchmarks can tune the exploit/explore tradeoff without
  rebuilding. Track as a cleanup item before Phase 5a.

- **Investigate why UCB-DAG underperforms uniform-DAG on this
  benchmark.** Could be a budget-allocation issue (100 gens is too
  small to afford UCB's concentration), a benchmark-specific fitness
  landscape pathology, or a signal that needs further tuning. Run a
  larger-budget experiment (e.g. 500 or 1000 generations) to see if
  UCB catches up once it has enough budget to exploit its signal.

- **Bring chain and monolithic benchmarks onto the same selection
  mode as DAG.** Either add `evolve-mce-select true` to both, or
  make it the default for Tier B variants. The current mixed-mode
  comparison is not strictly apples-to-apples.

- **"Best seen during run" vs "best in population at end" metric.**
  `validate_mce.sh` parses the log for best child fitness ever
  observed, which can be misleading when the chain is stuck in a
  broken state and only *rarely* evaluates correctly. A better
  metric would be "best current impl at end of run," which needs
  `evolve-best-fitness` TIL word (tracked in the plan's P2 item).

---

## Decision: P1 cleared; Phase 5a can start

The signal differentiates with ratios > 2× on every seed.
Branch A of the P1 plan is satisfied. Phase 5a (topology mutations —
`insert_node`) is unblocked.

The open questions above (UCB vs uniform performance, benchmark
comparison fairness, c_explore tuning) are follow-ups, not blockers.
They should be revisited once Phase 5a produces its own benchmark
results, because topology mutations may change the fitness landscape
enough that the right signal and scheduling policy shift.
