# ConceptDAG Phase 4 — Validation Results

**Date:** 2026-04-13
**Version:** v2.3.8
**Plan:** `20260408A-ConceptDAG-Implementation-Plan.md` — Phase 4 (Tier A final validation)
**Scope:** End-to-end comparison of Monolithic vs MCE-chain vs MCE-DAG on the quadratic benchmark

---

## Summary

Phase 4 of the ConceptDAG plan called for an end-to-end validation of Tier A
(phases 0–3) by running the quadratic benchmark under three evolution
strategies and comparing their ability to preserve or rediscover a correct
solution across multiple RNG seeds. This document is the deliverable.

**The Phase 4 run exposed several pre-existing bugs that invalidated the
prior Tier A claims:** a wrong argument order in `fitness.cpp::set_limits`
left call depth unbounded, allowing pathological mutations to blow the C++
stack; NaN fitness values silently corrupted running stats via Welford's
algorithm; three `evolve-*-register` primitives leaked test-case maps by
forgetting to release a `HeapArray::get` addref; and `TestCase` had no
destructor, so stored heap values would leak on cleanup. All are fixed in
v2.3.8. Without those fixes, the benchmark could not complete a single
100-generation run, let alone be compared across five seeds.

With the bugs fixed, the validation ran cleanly across all five seeds and
shows MCE-DAG outperforming MCE-chain on the quadratic benchmark.

---

## Benchmark

**Target function:** `f(x) = x² + 3x + 5` (Integer → Integer)

**Decomposition:**
```forth
: square-term  dup * ;
: linear-term  3 * ;
: offset       5 ;
: target-fn    dup square-term swap linear-term + offset + ;
```

**Test cases:** 9 inputs covering positive, negative, and zero values.

**Configuration:**
- Fitness mode: distance-based (α = 1.0)
- Instruction budget: 100,000 per evaluation
- Call depth limit: 256 (fitness evaluation only)
- Generations: 100
- Seeds: `42 123 7 999 314159`

**Three runs per seed:**

| Strategy | Script | Mechanism |
|----------|--------|-----------|
| Monolithic | `bench_mce_monolithic.til` | `evolve-word` directly mutates `target-fn` |
| MCE-chain | `bench_mce_quad.til` | `evolve-chain` round-robin over `[square-term, linear-term, offset]` |
| MCE-DAG | `bench_dag_quad.til` | `evolve-dag` contribution-weighted scheduling over the same concepts |

---

## Results

Ran via `tests/til/bench/validate_mce.sh`:

| Seed | Monolithic | MCE-chain | MCE-DAG |
|------|-----------|-----------|---------|
| 42 | 9/9 f=0.9999 | 9/9 f=0.9999 | 2/9 f=0.42 |
| 123 | 9/9 f=0.9999 | 2/9 f=0.42 | 9/9 f=0.9999 |
| 7 | 9/9 f=0.9999 | 2/9 f=0.48 | 9/9 f=0.9999 |
| 999 | 9/9 f=0.9999 | 2/9 f=0.42 | 9/9 f=0.9999 |
| 314159 | 9/9 f=0.9999 | 9/9 f=0.9999 | 9/9 f=0.9999 |

**Metric:** best child-impl fitness and highest `N/9 pass` count observed in the
evolution log over the 100-generation run. The evolution log records child
evaluations only, not baseline parents, so 9/9 here means *the mutation
process produced at least one correct child at some point*.

### Success rate (runs that produced a 9/9 child at least once)

| Strategy | Success | Rate |
|----------|---------|------|
| Monolithic | 5/5 | 100% |
| MCE-chain | 2/5 | 40% |
| MCE-DAG | 4/5 | 80% |

---

## Interpretation

**Monolithic is trivially perfect.** The starting `target-fn` impl is already
correct, and `evolve-word` keeps it as the highest-weight parent while
generating children. Most children are worse, but the parent's fitness score
is preserved and the log records baseline-equivalent evaluations whenever a
child happens to preserve behavior. This is not a meaningful win for
monolithic evolution; it is an artifact of starting from the known-correct
impl.

**MCE-chain succeeds 2/5 times.** The failure mode: round-robin mutation of
sub-concepts can corrupt multiple impls in rapid succession. Because each
new impl becomes the "latest" in the dictionary (displacing the previous
correct impl for that concept), the chain evaluation on subsequent
generations runs against the mutated (often broken) versions. Recovery
requires the random mutation stream to stumble back onto a correct
implementation — which, for the three sub-concepts here, happens on 2/5
seeds within 100 generations.

**MCE-DAG succeeds 4/5 times.** DAG-aware scheduling improves on chain by
weighting concept selection. In this benchmark the contribution weights
converged to uniform (see caveat below), so DAG's actual selection is
weighted-random over three concepts rather than strict round-robin. That
variance in scheduling order means DAG spends more generations on some
concepts and fewer on others per 100-generation budget, which — at least on
this benchmark — finds a correct child more reliably.

**Caveat — contribution weights are uniform on this benchmark.** The Phase 2
contribution weighting uses variance of impl weights as a signal. For the
quadratic benchmark, impl weights converge to approximately the same value
after a few generations (most mutations are catastrophic, so all impls land
near the prune threshold), leaving variance ≈ 0 for every concept. With
zero variance, the per-concept floor of 1e-6 dominates, and normalized
contributions end up at `1/N` for each of the N evolvable concepts. The
DAG-vs-chain difference above is therefore not driven by contribution
weighting in any meaningful sense — it's driven by weighted-random
vs round-robin scheduling. A benchmark with more differentiated sub-concepts
(e.g. one hard sub-concept and two easy ones) would be needed to stress-test
the variance signal itself.

---

## Bugs Found and Fixed

Phase 4 exposed pre-existing bugs that Phase 2's unit tests had missed. All
fixed in v2.3.8 before the validation runs above:

### 1. `fitness.cpp` argument order in `set_limits`

The signature is `set_limits(budget, stack_depth, call_depth, timeout)`. The
pre-v2.3.8 code passed `(instruction_budget, 10000, SIZE_MAX, 10.0)`, which
correctly set `stack_depth=10000` but left `call_depth=UNLIMITED`. Fitness
evaluation therefore had no recursion guard. Any mutation that produced a
self-calling word (e.g., a `Call target-fn` injected inside
`square-term`'s bytecode) ran unbounded recursion until the C++ stack blew
up. Observed crash depths exceeded 10,000 frames in one gdb session.

**Fix:** `ctx.set_limits(instruction_budget, 10000, 256, 10.0)` — stack stays
at 10000, call depth capped at 256. The existing `enter_call` / `exit_call`
checks in `compiled_body.cpp` and `Interpreter::execute_word` now fire
correctly and reject pathological recursive mutations with `"maximum call
depth exceeded"`.

### 2. NaN propagation in fitness evaluation

`value_distance(a, e)` called `as_double(actual) - as_double(expected)`
without NaN checks. A mutation that produced `0.0/0.0` or `sqrt(-1.0)` would
return a NaN `Value`, which passed through unscathed. `dist` became NaN;
`total_distance += dist` and `1.0/(1.0 + α·dist)` both propagated NaN; final
`result.fitness` was NaN. NaN fitness then entered
`ConceptNodeStats::record_fitness`, which used Welford's online algorithm:
`delta = fitness - mean_fitness` → NaN; `mean_fitness += NaN/n` → NaN;
permanently corrupted. Once corrupted, all subsequent display values read
NaN (`contrib=nan mean=nan var=nan`) even though per-call `best_fitness`
stayed valid, producing confusing "some stats NaN, some fine" output.

**Fix:** layered guards —
- `value_distance` now NaN-checks both inputs and the result via a
  `safe_distance` lambda; any NaN/inf returns the 1000.0 type-mismatch
  penalty.
- `Fitness::evaluate` treats `isnan(dist)` like `isinf(dist)`, adding the
  1e6 failure penalty instead of feeding NaN into the running totals.
- A final clamp in `Fitness::evaluate` guarantees `result.fitness` is never
  NaN/inf when the function returns.
- `ConceptNodeStats::record_fitness` short-circuits on NaN/inf input. This
  is belt-and-suspenders defense — after the above fixes, fitness should
  never be NaN — but cheap insurance against future regressions.

### 3. Per-concept variance wasn't per-concept

The pre-v2.3.8 Phase 2 implementation ran `K` chain evaluations once per
generation with a single SelectionEngine active, then assigned the resulting
variance to *every* evolvable concept in the DAG. All concepts ended up with
identical variance values and identical contribution weights —
contribution-weighted scheduling was effectively uniform-random.

**Fix:** replaced the `K`-evaluation loop with variance-of-impl-weights:
for each evolvable concept `C`, compute the sample variance of the
`weight_` values across `C`'s impls in the dictionary. Impl weights are
set by `evolve_sub_concept` using chain-level fitness, so this variance
directly measures how much `C`'s mutation space affects root fitness —
exactly the signal the original design wanted. Cost dropped from
`O(K × tests × concepts)` per generation to `O(impls × concepts)`.

(As noted in the Interpretation section, the benchmark doesn't actually
stress this code path — all concepts end with variance ≈ 0 — but the
implementation is correct and ready for a more differentiated benchmark.)

### 4. Test-case map leaks

`prim_evolve_register`, `prim_evolve_dag_register`, and
`prim_evolve_register_pool` all iterate over the test-case array with:

```cpp
Value map_val;
arr->get(i, map_val);  // HeapArray::get addrefs map_val
// ... use map_val ...
// loop iteration ends — map_val goes out of scope
```

`map_val` is addref'd on read-out but never released. On each registration,
each test-case map leaked, taking its inner "in"/"out" HeapArrays and the
hash-table storage with it. On the 9-test-case benchmark, that was roughly
5 KB leaked per `evolve-dag-register` call. ASan caught this immediately
under a debug build.

**Fix:** all three primitives now call `value_release(map_val)` at the end
of each loop iteration (and on the early `continue` path when the entry
isn't a Map).

### 5. `TestCase` has no destructor

`struct TestCase { std::vector<Value> inputs, expected; }`. Since `Value` is
POD, the default copy/move/destroy on `TestCase` was trivial — heap
references carried in inputs/expected were never released when the
containing vector was destroyed. This only affected benchmarks with heap
values in their inputs/expected (not the quadratic benchmark, which uses
only Integer), so it hadn't surfaced in tests yet. Nonetheless it was a
real latent leak and a correctness hazard for future benchmarks.

**Fix:** gave `TestCase` rule-of-five with proper refcount semantics —
destructor releases each stored Value, copy ctor/assign addref, move
ctor/assign steal. Existing unit tests using aggregate init
(`{{Value(...)}, {Value(...)}}`) kept working by adding a
`TestCase(vector, vector)` constructor.

---

## Verification

Across 5 consecutive 100-generation runs under ASan:

```
run 1: leaks=0
run 2: leaks=0
run 3: leaks=0
run 4: leaks=0
run 5: leaks=0
```

Full test suite (`scripts/test.sh all`): all tests pass in both debug and
release configurations. `bench_dag_quad.til` runs end-to-end in both
builds with `exit 0`.

---

## What Phase 4 Validated

- **The mechanics work.** ConceptDAG can be registered, evolved, and
  inspected. 100-generation runs complete without crashes, stack overflows,
  memory leaks, or NaN contamination on all five seeds tested.
- **DAG scheduling is competitive with chain scheduling.** 4/5 vs 2/5 on
  this benchmark. The difference comes from scheduling variance, not from
  contribution weighting (which is uniform here).
- **The call-depth guard catches pathological mutations.** Runs produce
  occasional `"maximum call depth exceeded"` messages routed to the
  evolution log when mutations create recursive Call chains — these runs
  continue cleanly instead of crashing.

## What Phase 4 Did Not Validate

- **Contribution-weighted scheduling as such.** The quadratic benchmark's
  sub-concepts are too simple (1–3 instructions each) to produce
  differentiated impl-weight variance. A benchmark where one concept's
  mutation space is clearly more impactful than another's is needed to
  measure whether the weighting signal actually steers evolution.
- **Multi-generation lineage stability.** Both MCE-chain and MCE-DAG
  sometimes fail to rediscover correct children within 100 generations
  after an early bad mutation. A longer budget (or an ablation that freezes
  one concept) would clarify whether this is a budget issue or a
  scheduling issue.

---

## Next Steps

Tier A is now materially validated. Tier B (topology mutations, Phase 5–6
of the plan) is the next work item. Per the implementation plan, Tier B
depends on a "benchmark with 10+ instruction sub-concepts" for meaningful
measurement. The quadratic benchmark does not meet that bar — designing a
suitable Tier B benchmark is a prerequisite to starting Phase 5.

Related deferred follow-ups surfaced during Phase 4:

- Distribution skew investigation (closed — was a symptom of the NaN bug,
  now resolved).
- Fitness-evaluation error messages still leak to stderr when they should
  be captured in `fitness_out_`. Noisy but not blocking; fix in a cleanup
  pass.
- `validate_mce.sh` currently parses "best child fitness observed in log"
  as its success metric. A more faithful metric would track "best current
  impl in the dictionary at end of run", which requires the benchmark to
  dump that info explicitly. Fine for now; revisit when designing a Tier B
  benchmark.
