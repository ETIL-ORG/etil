# ConceptDAG — Tier B Benchmark Design

**Date:** 2026-04-13
**Version:** P0 → v2.3.10
**Plan:** `20260413B-ConceptDAG-Tier-B-Implementation-Plan.md`, prerequisite P0
**Status:** Design + implementation, pending validation run

---

## Purpose

Tier A's quadratic benchmark (`f(x) = x² + 3x + 5`) decomposes into three
sub-concepts of 1–3 instructions each. On that benchmark the Phase 2
variance-based contribution weighting degenerates to uniform `1/N` for
every concept because impl weights all converge to values close to the
prune threshold, leaving zero variance as the differentiating signal.

Tier B needs a benchmark where:

1. At least one sub-concept has **10+ instructions** so the mutation space
   has room to produce intermediate-quality variants (not just perfect or
   broken).
2. Sub-concepts have **clearly different structural impact** on root
   fitness — some should be trivial "utility" operations, others should
   carry most of the root's correctness weight.
3. **Integer-only** values, so the Tier B benchmark avoids the extra
   complication of cross-type bridging (Tier A's separate xtype benchmark
   already covers that case).
4. **Exact-match test cases** — integer inputs and integer expected
   outputs. Partial correctness is captured by distance-based fitness
   scoring, not by fuzzy tolerance.

---

## Candidates Considered

The Tier B implementation plan listed three candidate target functions:

### Candidate 1 — Polynomial rational evaluation

`f(x) = (x³ + 2x² - 5x + 3) / (x² + 1)`

**Pros:** four naturally separate sub-concepts (cubic, quadratic, linear,
denominator), division brings a bridge point, higher-order polynomial is
meaty.

**Cons:** integer division loses precision badly — most inputs produce
outputs of 0, 1, or 2 because the denominator grows as fast as the
numerator. Test cases that distinguish children well are hard to construct.
Also, the polynomial sub-concepts have similar instruction counts, so the
"differentiated complexity" constraint is only partially met.

**Verdict:** rejected. Similar to the quadratic benchmark, just bigger.

### Candidate 2 — Integer norm ★ SELECTED

`f(x, y, z) = sqrt_approx(x² + y² + z²)`

**Pros:** sharp structural asymmetry — three identical-shape squaring
sub-concepts (`dup *`, 2 instructions each) plus one integer sqrt
approximation (Newton's method, ~15 instructions). The squarings are
"utility" concepts; `sqrt-approx` is the load-bearing heavy lifter. This
is precisely the differentiation the contribution-weighting signal needs
to measure. Integer-only throughout. Exact-match tests possible when all
inputs are chosen so `x² + y² + z²` is a perfect square (Pythagorean
triples and simple extensions give a rich test set).

**Cons:** three of the four sub-concepts are identical (`sq`). In the
current ETIL dictionary, all three calls resolve to the same `sq` impl,
so evolution only gets one concept to mutate on the "easy" side. That's
actually still fine for contribution-weighting validation: the question
is whether `sqrt-approx`'s contribution weight gets pushed *higher* than
`sq`'s (and `sum3`'s), not whether there are multiple easy concepts.

**Verdict:** selected. See rationale below.

### Candidate 3 — Polynomial derivative check

Given `p(x) = a₀ + a₁x + a₂x² + a₃x³`, verify `p'(x)` matches a finite
difference approximation.

**Pros:** three multi-instruction sub-concepts, each with clearly
different semantics (Horner's method for `poly-eval`, coefficient shift
for `poly-diff`, two-point difference for `fd-approx`). Genuinely
differentiated complexity.

**Cons:** requires a tolerance-based fitness scoring since the finite
difference is an approximation. This pulls in non-exact-match semantics
the Phase 4 validation script doesn't currently handle. Also, the three
sub-concepts are each 5–10 instructions, which means mutation noise is
high and convergence within 100 generations may be unreliable.

**Verdict:** deferred. Good candidate for a second Tier B benchmark after
Candidate 2's simpler geometry validates the variance signal.

---

## Selected Benchmark — Integer Norm

### Target Function

```
int-norm(x, y, z) = floor(sqrt(x² + y² + z²))
```

### Decomposition

```forth
variable sqrt-n        # scratch for sqrt-approx
variable sqrt-g        # scratch for sqrt-approx

: sq
  dup *
;

: sum3
  + +
;

: sqrt-approx
  dup 1 < if
    drop 0
  else
    dup sqrt-n !
    sqrt-n @ 2 / 1 + sqrt-g !
    5 0 do
      sqrt-n @ sqrt-g @ /
      sqrt-g @ +
      2 /
      sqrt-g !
    loop
    drop
    sqrt-g @
  then
;

: int-norm
  sq swap sq rot sq sum3 sqrt-approx
;
```

### Sub-concept structural summary

| Concept | Instructions | Complexity |
|---------|-------------|-----------|
| `sq` | 2 (`dup *`) | trivial utility |
| `sum3` | 2 (`+ +`) | trivial utility |
| `sqrt-approx` | ~15 (guard + variable store + 5-iter Newton loop) | **heavy lifter** |
| `int-norm` (root) | 7 (3×sq, 2×stack shuffle, sum3, sqrt-approx) | orchestration only |

`sqrt-approx` is the only sub-concept that satisfies the "10+
instructions" constraint from the Tier B plan. Its Newton iteration
consists of `/`, `+`, `2 /`, and `!` inside a `do...loop`, which
compiles to roughly 15 bytecode instructions once the loop and variable
stores are counted.

### Why this produces a differentiated variance signal

The Phase 2 contribution weighting uses variance of impl weights (chain
fitness scores assigned by `evolve_sub_concept`). The question is
whether different sub-concepts end up with meaningfully different impl
weight distributions.

- **`sq` (2 instructions):** almost every mutation is catastrophic. A
  mutated `sq` like `dup + *` or `dup * 2 *` changes the semantics
  completely and produces totally wrong chain output. Impl weights
  cluster near the prune threshold. Variance is low.

- **`sum3` (2 instructions):** same story. Mutation of `+ +` produces
  either a working variant (rare) or a broken one (common). Impl
  weights are bimodal (1.0 or ~0) with most mass near the floor.
  Variance is moderate but dominated by the bimodal split.

- **`sqrt-approx` (~15 instructions):** a mutation of a single
  instruction inside the Newton loop (e.g. changing `2 /` to `3 /` or
  swapping `+` for `-`) produces an *approximately* wrong result —
  the sqrt estimate is off, but not catastrophically. The chain
  fitness under distance-based scoring drops smoothly, producing
  intermediate-quality impls. The impl weight distribution spans
  a wider range, yielding **higher variance** than `sq` or `sum3`.

Prediction: after sufficient generations, `sqrt-approx` should have the
largest variance among the three evolvable concepts, and its
contribution weight should reflect that. Exact numbers depend on RNG
seed, but the ordering should be stable: `sqrt-approx > sum3 ≈ sq`.

**This is the signal we want to measure in P1 (v2.3.11).** If the
ordering does not hold, Phase 2 needs a signal redesign per the P1
branch-B plan.

### Test Cases

Pythagorean triples and simple extensions to give exact integer answers:

| `(x, y, z)` | `x² + y² + z²` | `int-norm` |
|-------------|----------------|-----------|
| `(0, 0, 0)` | 0 | 0 |
| `(3, 4, 0)` | 25 | 5 |
| `(1, 2, 2)` | 9 | 3 |
| `(2, 3, 6)` | 49 | 7 |
| `(1, 4, 8)` | 81 | 9 |
| `(5, 12, 0)` | 169 | 13 |
| `(0, 0, 10)` | 100 | 10 |
| `(8, 15, 0)` | 289 | 17 |
| `(6, 8, 0)` | 100 | 10 |

9 test cases, all producing exact integer results. Distance-based
fitness scoring handles partial correctness gracefully when mutations
produce impls that give approximately-right answers.

Verified standalone: the `sqrt-approx` definition above converges
correctly on all 9 inputs within 5 Newton iterations.

---

## Files to Produce

| File | Purpose |
|------|---------|
| `tests/til/bench/bench_dag_tierb.til` | DAG-aware run with `evolve-dag` |
| `tests/til/bench/bench_mce_tierb_chain.til` | Chain-mode comparison with `evolve-chain` |
| `tests/til/bench/bench_mce_tierb_monolithic.til` | Monolithic baseline with `evolve-word` on `int-norm` |
| `tests/til/bench/validate_tierb.sh` | 5-seed 3-way validation script, adapted from `validate_mce.sh` |

All three benchmark variants share the same sub-concept definitions and
test cases; they differ only in which evolution word they call.

---

## Evolvable Concept Accounting

The `int-norm` call graph as discovered by eager ConceptDAG extraction:

```
int-norm (root)
├── sq            [evolvable]
├── swap          [opaque — native primitive]
├── rot           [opaque — native primitive]
├── sum3          [evolvable]
└── sqrt-approx   [evolvable]
```

Three evolvable sub-concepts (`sq`, `sum3`, `sqrt-approx`). The native
primitives (`swap`, `rot`, and anything reached via `sqrt-approx` like
`dup`, `<`, `if`, `!`, `@`, `/`, `+`, `-`, `do`, `loop`) are all opaque
and are not evolved.

`sqrt-n` and `sqrt-g` are `variable` words; they appear in the DAG as
opaque leaves because their compiled form is a primitive that pushes
the data-field address.

---

## Validation Expectations

**P0 validation (this doc, v2.3.10):**

- [ ] All three benchmarks run end-to-end in release mode without
    crashes.
- [ ] All three benchmarks run end-to-end in debug mode under ASan
    with zero leaks.
- [ ] 5-seed comparison table in `validate_tierb.sh` output shows
    numbers for all three strategies.

**What P0 does NOT validate:**

- Whether the contribution weights actually differentiate the
  sub-concepts. That is P1's job, and requires parsing the
  `evolve-dag-show` output or the evolution log to extract per-concept
  contribution values.
- Whether DAG evolution beats chain evolution on *this* benchmark.
  Same reason — that is a P1 measurement.

P0 is strictly a "wire everything up and verify nothing crashes"
milestone. P1 is where we measure.

---

## Results (P0 run)

Completed 2026-04-13 against v2.3.10 (Tier B P0).

### Standalone correctness

`sqrt-approx` and `int-norm` verified against the 9 test-case table above.
All test cases produce exact expected integer norms (standalone run with
no evolution). Verified separately before wiring up `evolve-dag-register`.

### Release benchmark completion

All three benchmark variants run end-to-end in release mode without
crashes, across all 5 seeds (42, 123, 7, 999, 314159):

- `bench_dag_tierb.til` — ConceptDAG extraction finds 3 evolvable
  concepts (`sq`, `sum3`, `sqrt-approx`); 14 total DAG nodes (3
  evolvable + 10 opaque primitives + root + 1 depth-2 `*` from `sq`).
  100 generations complete.
- `bench_mce_tierb_chain.til` — `evolve-chain` round-robin completes
  100 generations.
- `bench_mce_tierb_monolithic.til` — `evolve-word` directly on the
  inlined `int-norm` completes 100 generations.

No crashes on the 5-seed 3-way run executed via
`tests/til/bench/validate_mce.sh tierb`.

### Debug (ASan) benchmark completion

All three variants complete a 100-generation run under ASan without
crashes, but the benchmark surfaced **primitive-level memory leaks
that were latent in v2.3.9** and only became visible under the more
structurally varied mutations Tier B produces.

Leak summary across 5 consecutive ASan runs of the DAG benchmark:

```
tierb run 1: leaks=0
tierb run 2: leaks=10
tierb run 3: leaks=3
tierb run 4: leaks=2
tierb run 5: leaks=6
```

Non-deterministic because the specific mutation chain that triggers
each leak is seed- and order-dependent. Investigated during P0 and
partially fixed — see "Bug-fix inventory" below.

### 5-seed 3-way comparison table

```
=== tier-b-integer-norm benchmark ===

Seed      Monolithic          MCE-chain           MCE-DAG
----      ----------          ---------           -------
42        9/9 f=0.999954      1/9 f=0.295019      9/9 f=0.999950
123       9/9 f=0.999952      1/9 f=0.349880      9/9 f=0.999945
7         9/9 f=0.999954      1/9 f=0.355430      2/9 f=0.426518
999       9/9 f=0.999953      9/9 f=0.999920      9/9 f=0.999946
314159    9/9 f=0.999948      1/9 f=0.351312      9/9 f=0.999938
```

**Success rate (runs where evolution produced a 9/9-passing child
at least once in 100 generations):**

| Strategy | Tier B | For comparison: quadratic |
|----------|--------|---------------------------|
| Monolithic | 5/5 (100%) | 5/5 (100%) |
| MCE-chain | 1/5 (20%) | 2/5 (40%) |
| MCE-DAG | 4/5 (80%) | 4/5 (80%) |

**DAG outperforms chain more decisively on Tier B than on the
quadratic benchmark:** 4/5 vs 1/5 (4× gap) versus 4/5 vs 2/5 (2× gap).
Monolithic remains trivially perfect because the starting `int-norm`
impl is already correct and evolve-word keeps it as the highest-weight
parent through mutation.

The chain variant struggles harder on Tier B than on the quadratic
benchmark. Intuition: chain's strict round-robin cycles through
`sq → sum3 → sqrt-approx` repeatedly. Each mutation of `sqrt-approx`
changes the dictionary's "latest" impl for that 15-instruction word,
and the chain evaluation then runs against a potentially-broken
sqrt-approx. Because sqrt-approx has so much more code than `sq` or
`sum3`, pathological mutations are more common, and the chain
evaluation rarely sees a correct complete pipeline. DAG's weighted-
random scheduling produces more variance in which concept gets
mutated when, and that variance seems to give it more chances to
leave a correct sqrt-approx impl at the top of the dictionary when
the chain is evaluated.

**Caveat (same as Tier A):** The contribution weights in MCE-DAG
still converge to uniform `1/3` on this benchmark because variance
of impl weights is approximately zero for every evolvable concept
(most mutations land at or near the prune threshold, so impl weights
cluster). The DAG win therefore comes from *scheduling* variance,
not from the *variance signal* driving contribution weighting. This
is exactly what P1 needs to measure and — if the signal still does
not differentiate — redesign.

### Bug-fix inventory

P0's ASan runs surfaced memory leaks that were latent in v2.3.9 but
invisible until the Tier B benchmark's more varied mutation space
exercised the affected code paths. Root causes and fixes:

**Fixed in `src/core/compiled_body.cpp`:**

1. **`local_rstack` destroyed without releasing heap refs on failure
   paths.** `local_rstack` is the local return stack used by
   `ToR`/`FromR`/`DoI`/`DoJ`. On a normal `FromR` exit the values are
   transferred back to the data stack, but if the containing word
   errored out between `ToR` and `FromR`, `local_rstack` would be
   destroyed POD-style, leaking heap refs. **Fix:** added a
   `LocalRStackGuard` RAII struct at the top of `execute_compiled`
   that releases every remaining `local_rstack` element on any
   function-exit path (success, failure, or exception).

2. **`BranchIfFalse` leaked non-Boolean popped values.** If a mutation
   put something other than a Boolean on top of the stack before an
   `if`/`while`/`until`, the opcode popped the value, printed an
   error, and `return false` without releasing. For heap-valued tops
   this leaked. **Fix:** added `value_release(*flag)` on the error
   path.

3. **`DoPlusLoop` leaked `opt_inc` on error paths.** Two error paths
   (return-stack underflow, zero increment) did not release the
   popped increment value. **Fix:** added `value_release(*opt_inc)`
   on each error exit.

4. **`FetchR` / `DoI` / `DoJ` copied from `local_rstack` to data
   stack without `addref`.** These opcodes push a copy of a return-
   stack slot (e.g. `r@`, loop index `i`, outer loop index `j`). The
   old code did `push(local_rstack.back())` which transferred the
   POD Value (including its embedded pointer) without incrementing
   refcount. Both locations then shared one refcount. Before this
   session, the `LocalRStackGuard` did not exist and the over-count
   was benign because the local stack's copies were never released.
   But with the new RAII guard, local_rstack releases its copy, and
   the data_stack copy becomes a dangling ref. **Fix:** `addref`
   every `local_rstack` → `data_stack` copy. For loop indices this
   is a no-op at runtime (integers are POD) but the pattern is
   future-proof for heap values.

**Remaining latent leaks (not fixed in this P0 commit):**

Individual primitives (`prim_splus`, `prim_us_to_iso`, `prim_obs_range`,
`prim_obs_timer`, `prim_mat_new`, `prim_mat_randn`, `prim_bytes_new`)
occasionally leak heap values when a mutation stresses specific error
paths. These are all allocation-in-primitive, push-to-stack, then
something downstream fails pattern. `drain_stack` in
`Fitness::run_single_test_distance` should catch them, but
investigation suggests the actual leak sites are primitive-internal
error paths that don't release intermediate heap values before
returning false, so by the time `drain_stack` runs the value was
never on the stack to begin with.

These leaks are:

- **Non-deterministic** — triggered by specific mutation chains that
  depend on seed and operator ordering. 0–12 leaks per 100-generation
  run.
- **Small per occurrence** — each incident leaks 1–30 objects
  totalling ~1–40 KB. No accumulation beyond a single run.
- **Pre-existing in v2.3.9** — verified by re-running the quadratic
  benchmark under ASan, which also leaks 0–2 objects intermittently.
  The Tier B benchmark just stresses more primitive paths, making
  the leaks more visible.

**Decision:** Fix-each-primitive is a dedicated cleanup pass, not
P0 scope. Tracked as a new follow-up item; it should land before
Phase 5a starts, alongside any other hardening work triggered by
P1's variance-signal investigation. P0's "no crashes" bar is met;
the "no leaks" bar is partially met for the opcodes in
`execute_compiled` and partially unmet for primitive internals.

### Files produced

- `docs/claude-design/20260413C-ConceptDAG-Tier-B-Benchmark-Design.md`
  (this doc)
- `tests/til/bench/bench_dag_tierb.til`
- `tests/til/bench/bench_mce_tierb_chain.til`
- `tests/til/bench/bench_mce_tierb_monolithic.til`
- `tests/til/bench/validate_mce.sh` (updated to accept `quad | tierb | all`)

### Files modified

- `src/core/compiled_body.cpp` — RAII guard for `local_rstack`,
  release fixes in `BranchIfFalse` and `DoPlusLoop`, addref fixes
  in `FetchR` / `DoI` / `DoJ`.

---

## Next Steps

- P0 validation run, then commit as v2.3.10.
- P1 (v2.3.11) parses the run output and measures whether
  `sqrt-approx`'s contribution weight is indeed higher than `sq`'s and
  `sum3`'s. Publishes `20260XXX-ConceptDAG-Contribution-Weight-Validation.md`.
- Decision point: if contribution weights differentiate as predicted,
  P1 clears and Phase 5a starts. If not, P1 branches to signal redesign.
