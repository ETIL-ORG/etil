# ConceptDAG — Tier B (Phase 5+) Implementation Plan

**Date:** 2026-04-13
**Version basis:** v2.3.9 (Tier A validated)
**Supersedes:** `20260408A-ConceptDAG-Implementation-Plan.md` — Phase 5 and Phase 6 sections
**Informed by:** `20260413A-ConceptDAG-Phase-4-Validation-Results.md`
**Status:** Plan

---

## Context: Where Tier A Left Us

Tier A (Phases 0–4) is materially validated as of v2.3.9. The ConceptDAG
data structure, eager extraction, DAG-aware scheduling, contribution
weighting, logging, and the `evolve-dag*` TIL words all work end-to-end.
The 100-generation quadratic benchmark runs cleanly in both debug (ASan)
and release builds, and a 5-seed comparison shows MCE-DAG (4/5 perfect
runs) outperforming MCE-chain (2/5) on the quadratic decomposition.

**Tier A validation also exposed bugs that had been latent since v2.3.5**,
when Phase 4 was first declared complete without end-to-end benchmark
runs. Those bugs — a `set_limits` argument-order mistake that left fitness
evaluation without a recursion guard, NaN propagation through Welford's
online variance, a wrong-scope per-concept variance implementation, and
`HeapArray::get` addrefs never released in the three `evolve-*-register`
primitives — were all fixed in v2.3.8. The full account is in
`20260413A-ConceptDAG-Phase-4-Validation-Results.md`.

**Equally important: the quadratic benchmark does not actually stress the
Phase 2 contribution-weighting signal.** Sub-concept impl weights all
converge to near-identical values because the sub-concepts are 1–3
instructions each and most mutations are catastrophic. Variance ends up
at approximately zero for every concept, contributions normalize to
uniform `1/N`, and DAG's scheduling degenerates to weighted-random over
all concepts. The measured MCE-DAG / MCE-chain advantage on the quadratic
benchmark therefore comes from weighted-random vs round-robin scheduling
*variance*, not from the variance *signal*. That is — at best — half of
what Phase 2 was supposed to demonstrate.

Tier B is where ConceptDAG graduates from "mechanically functional" to
"measurably useful." That requires changes to what we benchmark and how
we validate, not just new code.

---

## Lessons from Tier A That Shape Tier B

### 1. Unit tests are not integration validation.

v2.3.5 had unit tests passing for every Tier A phase, and the
`test_concept_dag.til` integration test (5 generations) also passed. Both
missed the fitness-eval recursion guard bug, the NaN propagation bug, and
the per-concept variance design flaw — because none of them stressed the
system long enough or hard enough to trigger mutation pathologies, and
none checked whether contribution weights were actually differentiated.

**Rule for Tier B:** Every phase must produce a passing end-to-end
benchmark run (100+ generations, 5+ seeds, ASan + release) before the
phase is declared complete. Unit tests are table stakes, not the gate.

### 2. Benchmarks must match the feature being tested.

A benchmark with uniform sub-concept complexity cannot differentiate a
contribution-weighting algorithm. A benchmark where all mutations land
near the prune threshold cannot differentiate selection strategies
either. Tier B features — topology mutations, subtree crossover, bloat
control — will all be invisible on the quadratic benchmark for similar
reasons.

**Rule for Tier B:** A new benchmark with differentiated sub-concept
complexity is a hard prerequisite. Phase 5 cannot start against the
quadratic benchmark.

### 3. "Shipped" and "validated" are not the same.

v2.3.5 was declared shipped with Phase 4 "done," but the deliverable
artifacts (4-way `validate_mce.sh`, documented benchmark comparison,
updated README section) were not actually produced. A future session
discovered this four weeks later when the benchmark finally ran. That
is a process failure, not a technical one.

**Rule for Tier B:** Each phase has an explicit deliverable checklist in
this document, and the phase is not "complete" until every item is
checked, including benchmark results written to `docs/claude-design/`.
The super-push script should not be run until the deliverables exist.

### 4. Layered safety > single guards.

The recursion guard bug was a single-point-of-failure: one miscalled
`set_limits` left the entire fitness evaluator unbounded, and the bug hid
because neither the AST mutation layer nor `execute_compiled` had a
backup check. The NaN propagation was similar: one missing `isnan` check
in `value_distance` corrupted running stats for every downstream consumer.

**Rule for Tier B:** New code that creates execution contexts, runs
evolved bytecode, or stores statistics must have *layered* defenses —
not because the inner layers are unreliable, but because one missed
check should never cascade into silent correctness failures.

---

## Tier B Prerequisites (P-phases)

These are not optional and must complete before Phase 5 starts. They
do not bump the minor version; they are patch releases on the current
line (v2.3.x) and live on the `concept-dag-topology` feature branch.

### P0 — Benchmark with Differentiated Sub-Concept Complexity

**Version:** v2.3.10
**Estimated effort:** 1–2 days
**Deliverable:** new benchmark + decomposition design doc

Tier A's quadratic benchmark decomposes `f(x) = x² + 3x + 5` into three
1–3 instruction sub-concepts. Tier B needs a benchmark where sub-concepts
have clearly different fitness impact, so the variance signal has
something to measure.

**Design constraints:**

- At least one sub-concept with **10+ instructions** (the
  implementation plan explicitly names this as a prerequisite).
- At least three sub-concepts with **clearly different complexity**
  so their mutation spaces differ in how much they affect root fitness.
- At least one sub-concept that is **structurally important** (heavy
  fitness impact) and at least one that is **structurally minor** (small
  fitness impact). Variance-based contribution weighting should
  distinguish these.
- Integer-only values (to avoid cross-type complications in Tier B).
- Test cases that exercise multiple branches of each sub-concept, so
  mutations that break one path but preserve another produce
  intermediate fitness rather than binary pass/fail.

**Candidate target functions (to be evaluated in the design doc):**

- **Polynomial rational evaluation:** `f(x) = (x³ + 2x² - 5x + 3) / (x² + 1)`.
  Numerator has three polynomial sub-concepts of varying complexity; the
  denominator is a distinct sub-concept with its own type-preservation
  constraints; the division brings in a bridge point.
- **Integer norm:** `f(x, y, z) = sqrt_approx(x² + y² + z²)`. Three
  identical-structure squaring sub-concepts (each `dup *`) plus a
  multi-instruction `sqrt_approx` (Newton's method, 8–12 instructions).
  Squarings are structurally minor; `sqrt_approx` is the heavy lifter —
  an ideal stress test for contribution weighting.
- **Polynomial derivative check:** given `p(x) = a₀ + a₁x + a₂x² + a₃x³`,
  verify `p'(x)` matches a finite-difference approximation. Sub-concepts:
  `poly-eval` (multi-term Horner's method, 10+ instructions),
  `poly-diff` (coefficient shift), `fd-approx` (finite difference).
  Each concept has a distinct, measurable impact on accuracy.

**Deliverable artifacts (P0):**

- [ ] `docs/claude-design/20260414A-ConceptDAG-Tier-B-Benchmark-Design.md`
    — decision doc selecting one of the candidates (or a new one), with
    rationale. Tables showing instruction counts per sub-concept,
    expected variance differentiation, and test-case coverage.
- [ ] `tests/til/bench/bench_dag_tierb.til` — the benchmark file itself.
- [ ] `tests/til/bench/bench_mce_tierb_chain.til` — chain-mode variant
    for comparison.
- [ ] `tests/til/bench/bench_mce_tierb_monolithic.til` — monolithic
    baseline for comparison.
- [ ] `tests/til/bench/validate_tierb.sh` — or an extension of
    `validate_mce.sh` that handles both the quadratic and Tier B
    benchmarks.
- [ ] 100-generation run in both debug + release, under ASan, 5 seeds,
    zero leaks, zero crashes. Results captured in the design doc.

### P1 — Validate the Variance Signal

**Version:** v2.3.11
**Estimated effort:** 1 day
**Deliverable:** validation results doc

Run the P0 benchmark and measure whether contribution weights actually
differentiate sub-concepts. Specifically:

- **Expected signal:** the structurally heavy sub-concept (e.g.
  `sqrt_approx` in the integer-norm candidate) should receive a
  clearly higher contribution weight than the structurally minor ones.
- **Expected magnitude:** if weights end up within ±10% of uniform,
  the signal is essentially absent and Phase 2 needs redesign.

**Two branches based on what the data shows:**

**Branch A — signal works.** Contribution weights differentiate
sub-concepts meaningfully (strongest > weakest by at least 2×). Proceed
to Phase 5 as planned.

**Branch B — signal does not work.** Weights converge to uniform even
on a differentiated benchmark. Two sub-options:

- **B.1 — redesign the signal.** Instead of variance-of-impl-weights,
  try *fitness-delta-on-mutation*: for each concept C, track the
  running mean of `Δfitness` observed when C was the mutation target.
  High-impact concepts produce larger deltas (positive or negative).
  This is closer to the original "variance-based credit" intent.
- **B.2 — decouple scheduling from signal.** Accept that contribution
  weighting isn't the right primary scheduler, and introduce an
  alternative: for example, UCB-style exploration that prioritizes
  concepts with fewer generations evolved. The variance signal becomes
  informational rather than controlling.

**Deliverable artifacts (P1):**

- [ ] `docs/claude-design/20260415A-ConceptDAG-Contribution-Weight-Validation.md`
    — measurement methodology, raw data, conclusion, branch taken.
- [ ] If Branch A: documentation that the current implementation is
    the Tier B baseline.
- [ ] If Branch B.1: redesigned `evolve_dag_generation()` variance
    computation + unit tests + re-validation.
- [ ] If Branch B.2: new scheduling primitive + `evolve-dag-schedule`
    word to switch between modes + validation of each mode.

### P2 — Success Metric Improvements

**Version:** v2.3.12 (optional, can roll into P1)
**Estimated effort:** half a day

Current `validate_mce.sh` parses "best child fitness observed in the
log" as its success metric. That captures mutation *attempts* but not
the *state of the population at end of run*. Two concrete fixes:

- [ ] New TIL word: `evolve-best-fitness ( concept-str -- x )`. Returns
    the weight of the top-ranked impl currently in the dictionary for
    `concept-str`. The benchmark emits this at end of run and the
    validation script parses it directly instead of grepping logs.
- [ ] New TIL word: `evolve-population-stats ( concept-str -- mean var max )`.
    Returns summary of the current impl population. Lets the validation
    script distinguish "we found a good child once" from "the
    population is consistently good."

Optional, but useful. Do this in P2 or defer to after Phase 5 — the
benchmark comparison is more faithful with these metrics but current
"best seen" works well enough for go/no-go decisions.

---

## Phase 5 — DAG Topology Mutations (Revised)

**Version:** v2.4.1 (minor bump — first Tier B feature)
**Prerequisites:** P0 and P1 (and P2 if taken) must be complete and
validated. Phase 5 **cannot start** against the quadratic benchmark.

The original plan grouped four topology mutations (`insert`, `remove`,
`duplicate`, `absorb`) into one phase and tested them all at once. That
is too big a step given Tier A's experience. Split into sub-phases with
validation gates between each, so we catch the inevitable bugs in one
operation before they compound.

### Phase 5a — `insert_node` (the easy one)

**Version:** v2.4.1
**Rationale:** Insertion is the most mechanical of the four operations.
It does not require type-repair reasoning beyond what BridgeMap already
provides, and it never deletes anything from the dictionary. Easy to
validate, hard to corrupt state.

**Mutation:**

1. Pick a random edge `(parent_impl, child_concept)` from the DAG.
2. Query `BridgeMap` for a type-compatible bridging concept.
3. Create the new concept in the dictionary with a bridge-word impl.
4. Rewrite parent's impl bytecode: `Call child_concept` →
   `Call new_concept`.
5. New concept's impl calls the original child concept.
6. Rebuild DAG (eager, from updated bytecode).

**Unit tests:**

- `InsertNodeBridgeable` — compatible types succeeds, new node appears
  in DAG, parent impl's bytecode is rewritten, call graph is correct.
- `InsertNodeIncompatible` — unbridgeable types fail gracefully, no
  dictionary mutation, no DAG corruption.
- `InsertNodeRebuildsDAG` — after insertion, `dag.build()` on the same
  root produces a DAG with the new node, correct depths, correct
  evolvable set.
- `InsertNodePreservesFitness` — before/after insertion, chain
  evaluation produces the same output on all test cases (insertion
  must be semantics-preserving until the new concept is itself
  mutated).
- `InsertNodeDoesNotRecurse` — the new concept cannot be the root or
  cause a cycle.

**Integration validation:**

- [ ] `test_dag_topology_insert.til` — registers DAG, performs N
    insertions explicitly (via a new `evolve-dag-insert` TIL word
    introduced here), verifies the DAG grows, verifies chain evaluation
    still produces correct results.
- [ ] 100-generation run on the Tier B benchmark with `dag_mutation_rate =
    0.1` (insertions-only mode — `remove`/`duplicate`/`absorb` not yet
    implemented), zero crashes, ASan clean, DAG size stays within
    `max_dag_nodes`.
- [ ] Comparison against the Phase 4 baseline: does insertion-only
    evolution help or hurt fitness on the Tier B benchmark? Result
    documented either way.

**New TIL word:**

- `evolve-dag-insert ( parent-concept child-concept -- flag )` —
  manual topology mutation for testing and TIL-level experiments.

**Deliverable artifacts (5a):**

- [ ] `include/etil/evolution/dag_genetic_ops.hpp` — class scaffold with
    `insert_node()` only. Other methods declared but unimplemented,
    returning `false` and setting an error.
- [ ] `src/evolution/dag_genetic_ops.cpp` — `insert_node()` implementation.
- [ ] Unit test file `tests/unit/test_dag_genetic_ops.cpp` with the
    insert tests above.
- [ ] `tests/til/test_dag_topology_insert.til` + `.sh`.
- [ ] `docs/claude-design/2026XXXX-DAG-Insert-Validation.md` — benchmark
    results.

### Phase 5b — `absorb_node` (simplest deletion)

**Version:** v2.4.2
**Rationale:** Absorb removes a concept by inlining it into its parent.
It is permanent, per the Tier A design decision. Because it only
operates on concepts with one parent and one impl, and only changes
ownership/lifetime (not type reasoning), it is the simplest of the
removal operations.

**Mutation:**

1. Pick a concept C with exactly one parent impl and one impl.
2. Inline C's bytecode into the parent at the Call site.
3. `dict.forget_all(C)`.
4. Rebuild DAG.

**Risk:** inlining can create duplicate variable names, interact with
`create`/`does>` words, and expand the parent's bytecode beyond any
per-impl size budget. All of these must be checked.

**Unit tests:**

- `AbsorbSingleInstructionConcept` — inline the simplest case.
- `AbsorbMultiInstructionConcept` — inline 5+ instructions, verify
  parent bytecode grows correctly.
- `AbsorbIsPermanent` — post-absorb, `dict.lookup(absorbed_name)`
  returns nullopt.
- `AbsorbRebuildsDAG` — new DAG omits the absorbed node.
- `AbsorbPreservesFitness` — absorbed chain evaluates identically
  before and after on all test cases.
- `AbsorbRespectsBloat` — absorb that would exceed `max_impl_bytes` is
  rejected.

**Integration validation:**

- [ ] `test_dag_topology_absorb.til` — exercises absorb explicitly.
- [ ] 100-generation run on Tier B benchmark with both `insert` and
    `absorb` enabled, each at `dag_mutation_rate = 0.05`. Verify DAG
    growth reaches a dynamic equilibrium (insertions balanced by
    absorbs).
- [ ] Before/after DAG comparison documented.

**Deliverables (5b):**

- [ ] `absorb_node()` in `dag_genetic_ops.cpp`.
- [ ] Unit tests added to `test_dag_genetic_ops.cpp`.
- [ ] `test_dag_topology_absorb.til` + `.sh`.
- [ ] `evolve-dag-absorb ( parent-concept child-concept -- flag )` TIL word.
- [ ] Validation results appended to the design doc from 5a (or new doc).

### Phase 5c — `remove_node` (type-reasoning deletion)

**Version:** v2.4.3
**Rationale:** Remove is trickier than absorb because it rewires the
parent to call C's children directly, which requires type compatibility
between parent's previous output type, C's children's input types, and
(possibly) a bridge insertion to reconcile mismatches. This is the
first topology mutation that can *fail for non-syntactic reasons*.

**Mutation:**

1. Pick a concept C with contribution weight below a threshold.
2. Check that C's parent output type can flow directly into C's
   children's input types, or that a bridge exists.
3. Rewrite parent's impl bytecode: replace `Call C` with calls to
   C's children (with bridge insertion if needed).
4. `dict.forget_all(C)`.
5. Rebuild DAG.

**Unit tests:** all the usual insert/absorb analogs, plus:

- `RemoveNodeTypeCompatible` — direct bypass succeeds.
- `RemoveNodeNeedsBridge` — bridge insertion succeeds where applicable.
- `RemoveNodeNoBridgeRejected` — unbridgeable removal fails cleanly.
- `RemoveNodeLowContributionOnly` — remove operation only fires on
  low-contribution concepts (threshold respected).

**Integration validation:**

- [ ] 100-gen run on Tier B benchmark with `insert`, `absorb`, and
    `remove` enabled.
- [ ] Verify the low-contribution concepts are actually the ones being
    removed. This is where the Tier B benchmark's differentiated
    complexity matters — if everything has uniform contribution, there
    is no "low-contribution concept" to target.

**Deliverables (5c):** analogous to 5a/5b.

### Phase 5d — `duplicate_node` (gene duplication)

**Version:** v2.4.4
**Rationale:** Gene duplication creates `C-dup-N` as a specialized copy
of `C`, replacing one reference in a parent impl. It's the most
structurally invasive operation because both copies evolve independently
afterwards — the DAG grows in a way that can't be simply undone.

**Mutation:**

1. Pick a concept C with contribution weight above a threshold.
2. Create `C-dup-N` in the dictionary with copies of C's impls (deep
   copy — each impl gets a new ID).
3. Pick one parent impl that calls C. Rewrite one `Call C` →
   `Call C-dup-N`.
4. Rebuild DAG.

**Naming:** `C-dup-1`, `C-dup-2`, ... with the counter stored in the
`ConceptDAG` (not a global, so multiple roots can duplicate the same
concept independently).

**Unit tests:** the usual analogs, plus:

- `DuplicateNodeIndependentEvolution` — after duplication, mutations to
  `C` do not appear in `C-dup-1` and vice versa.
- `DuplicateNodeReferencesOneParent` — the duplication only replaces
  one `Call C` site, not all of them.
- `DuplicateNodeHighContributionOnly` — duplication only fires on
  high-contribution concepts (threshold respected).

**Integration validation:**

- [ ] 100-gen run on Tier B benchmark with all four topology mutations
    enabled.
- [ ] Verify that high-contribution concepts are the ones being
    duplicated. Again, depends on the benchmark differentiating them.
- [ ] Measure whether duplication actually helps — i.e. does a
    duplicated high-impact concept eventually diverge into two
    specialized impls, and does that improve root fitness? This is the
    first validation of the full Tier B hypothesis.

**Deliverables (5d):** analogous, plus the first full Tier B benchmark
comparison in the design doc.

### Phase 5 Integration Mode

Once all four operations are in, `evolve_dag_generation()` gains a
topology-mutation fork:

```cpp
if (coin(rng) < config_.dag_mutation_rate) {
    perform_topology_mutation();  // randomly pick insert/remove/dup/absorb
} else {
    evolve_sub_concept(selected);  // AST-level mutation (original path)
}
```

Config:

```cpp
double dag_mutation_rate = 0.05;  // 5% topology mutations by default
```

The rate can be adjusted per benchmark via a new TIL word
`evolve-dag-mutation-rate! ( x -- )`.

---

## Phase 6 — Subtree Operations and Bloat Control (Revised)

**Version:** v2.4.5
**Prerequisites:** Phase 5 complete and validated.

The original Phase 6 plan is largely sound but should also be split
into sub-phases with validation gates.

### Phase 6a — Bloat Control

Enforce DAG growth limits before adding subtree operations, because
subtree operations amplify growth dramatically.

**Config additions:**

```cpp
size_t max_dag_depth = 5;
size_t max_dag_nodes = 20;
size_t max_impl_bytes = 4096;  // per-impl size limit for absorb
```

**Enforced in:**

- `insert_node()`: reject if depth would exceed max.
- `duplicate_node()`: reject if node count would exceed max.
- `absorb_node()`: reject if parent bytecode would exceed max_impl_bytes.
- `evolve_dag_generation()`: if DAG exceeds limits, force a remove or
  absorb instead of any growing mutation.

**TIL words:**

- `evolve-dag-max-depth! ( n -- )` / `evolve-dag-max-depth@ ( -- n )`
- `evolve-dag-max-nodes! ( n -- )` / `evolve-dag-max-nodes@ ( -- n )`
- `evolve-dag-max-impl-bytes! ( n -- )` / `evolve-dag-max-impl-bytes@ ( -- n )`

**Validation:**

- [ ] 100-gen run with aggressive `dag_mutation_rate = 0.2` and tight
    bounds (`max_dag_nodes = 15`). DAG size never exceeds 15. No
    crashes. Benchmark still produces valid evolved children.

### Phase 6b — Subtree Crossover and Duplication

The capstone operations. These move or duplicate entire sub-DAGs, so
they need bloat control already in place.

```cpp
bool subtree_crossover(ConceptDAG& target_dag,
                       const ConceptDAG& source_dag,
                       std::mt19937_64& rng);

bool subtree_duplicate(ConceptDAG& dag,
                       const std::string& subtree_root,
                       std::mt19937_64& rng);
```

Subtree crossover requires two registered DAGs (e.g. evolving two
related target functions in parallel) so the Tier B benchmark may need
a second related target function to enable this test.

**Validation:**

- [ ] Unit tests for type-compatible and incompatible subtree
    transplants.
- [ ] Integration test with two related benchmarks and subtree
    crossover enabled. Measure whether cross-pollination improves
    fitness on either.

---

## Tier B Super-Push Gates

Each phase has a non-negotiable gate:

| Gate | Requirement |
|------|-------------|
| Unit tests | All new tests pass; no existing test regresses |
| Integration test | Per-phase `.til` test passes in CTest |
| Debug benchmark | 100-gen run under ASan: zero leaks, zero crashes |
| Release benchmark | 100-gen run release mode: completes cleanly |
| 5-seed comparison | Validation script run with 5 seeds, results tabulated |
| Documentation | Per-phase validation doc in `docs/claude-design/` |

Only when **all six** are green does the phase get a version bump and
commit. The super-push script must not be run until the documentation
deliverable exists.

**Tier B overall gate:** Tier B ships if Phase 5 + Phase 6 produce
measurable benefits on the Tier B benchmark (P0). "Measurable" means:

- Statistically distinguishable from Phase 4 baseline across 5 seeds
  (one standard deviation separation at minimum).
- At least one scenario where topology mutations find a solution that
  pure AST mutation does not within the same generation budget.
- Contribution weighting drives at least one observable scheduling
  bias (high-contribution concepts get more generations).

If Tier B does not meet the gate, the code ships as "experimental" and
the implementation plan documents the limitation rather than being
retroactively declared successful.

---

## Revised Phase/Version Map

| Phase | Version | What | Branch | Status |
|-------|---------|------|--------|--------|
| P0 | v2.3.10 | Tier B benchmark design + implementation | `concept-dag-topology` | Not started |
| P1 | v2.3.11 | Contribution-weight validation | `concept-dag-topology` | Not started |
| P2 | v2.3.12 | Metric improvements (optional) | `concept-dag-topology` | Not started |
| 5a | v2.4.1 | `insert_node` + TIL word + validation | `concept-dag-topology` | Not started |
| 5b | v2.4.2 | `absorb_node` + TIL word + validation | `concept-dag-topology` | Not started |
| 5c | v2.4.3 | `remove_node` + TIL word + validation | `concept-dag-topology` | Not started |
| 5d | v2.4.4 | `duplicate_node` + TIL word + validation | `concept-dag-topology` | Not started |
| 6a | v2.4.5 | Bloat control | `concept-dag-topology` | Not started |
| 6b | v2.4.6 | Subtree operations | `concept-dag-topology` | Not started |

**Minor version 2.4.x is reserved for Tier B work.** If any P-phase
reveals that the Tier A foundation needs structural changes, those
changes land as 2.3.x patches before the 2.4.x line starts.

---

## New TIL Words (Tier B)

| Word | Stack Effect | Phase | Description |
|------|-------------|-------|-------------|
| `evolve-best-fitness` | `( concept-str -- x )` | P2 | Current best impl weight for a concept |
| `evolve-population-stats` | `( concept-str -- mean var max )` | P2 | Current population stats |
| `evolve-dag-insert` | `( parent-concept child-concept -- flag )` | 5a | Manual topology mutation: insert |
| `evolve-dag-absorb` | `( parent-concept child-concept -- flag )` | 5b | Manual topology mutation: absorb |
| `evolve-dag-remove` | `( concept-str -- flag )` | 5c | Manual topology mutation: remove |
| `evolve-dag-duplicate` | `( concept-str -- flag )` | 5d | Manual topology mutation: duplicate |
| `evolve-dag-mutation-rate!` | `( x -- )` | 5d | Set probability of topology vs AST mutation |
| `evolve-dag-max-depth!` | `( n -- )` | 6a | Max nesting depth |
| `evolve-dag-max-depth@` | `( -- n )` | 6a | Get max depth |
| `evolve-dag-max-nodes!` | `( n -- )` | 6a | Max concept count |
| `evolve-dag-max-nodes@` | `( -- n )` | 6a | Get max node count |
| `evolve-dag-max-impl-bytes!` | `( n -- )` | 6a | Per-impl size cap for absorb |
| `evolve-dag-max-impl-bytes@` | `( -- n )` | 6a | Get impl-size cap |

---

## File Inventory (New Files, Tier B)

| File | Phase | Purpose |
|------|-------|---------|
| `docs/claude-design/20260414A-ConceptDAG-Tier-B-Benchmark-Design.md` | P0 | Benchmark design doc |
| `tests/til/bench/bench_dag_tierb.til` | P0 | Tier B DAG benchmark |
| `tests/til/bench/bench_mce_tierb_chain.til` | P0 | Tier B chain comparison |
| `tests/til/bench/bench_mce_tierb_monolithic.til` | P0 | Tier B monolithic baseline |
| `tests/til/bench/validate_tierb.sh` | P0 | Tier B validation script (may extend `validate_mce.sh`) |
| `include/etil/evolution/dag_genetic_ops.hpp` | 5a | Topology mutation operators |
| `src/evolution/dag_genetic_ops.cpp` | 5a–5d | Topology mutation implementation |
| `tests/unit/test_dag_genetic_ops.cpp` | 5a–5d | Unit tests |
| `tests/til/test_dag_topology_*.til` | 5a–5d | Per-operation integration tests |

Plus per-phase validation docs in `docs/claude-design/` (one per phase).

---

## Risk Assessment (Updated)

| Risk | Phase | Mitigation |
|------|-------|------------|
| P0 benchmark still doesn't differentiate variance | P0 | P1's Branch B branches redesign the signal; either way P1 is a decision point |
| Insertion creates cycles | 5a | Pre-check that `new_concept` is not an ancestor of `child_concept` in the DAG |
| Absorb explodes parent bytecode size | 5b | `max_impl_bytes` hard cap enforced in bloat control (6a) — but 5b must at least check it per-op |
| Remove leaves dangling references | 5c | Rebuild DAG after every mutation; any reference to a removed concept triggers a validation failure |
| Duplicate creates name collisions | 5d | `ConceptDAG`-scoped counter; never reuses names |
| Duplicate forgets to deep-copy impls | 5d | Explicit unit test `DuplicateNodeIndependentEvolution` verifies both diverge |
| Topology mutations don't beat AST mutation | Tier B gate | Document as limitation, ship as experimental, don't retroactively declare victory |
| DAG rebuild after mutation is expensive | all | Tier A uses eager rebuild; optimize only if profiling shows hot — not before |

---

## Deferred Items (From Tier A)

Tracked here so they don't get lost:

- **Fitness evaluation errors leaking to stderr.** Should be captured in
  the thread-local `fitness_out_` like stdout is. Noisy but not
  blocking. Cleanup pass sometime during P-phases.
- **Phase 3 mid-evolution stats snapshot** — implemented but not
  validated in Phase 4. Flagging in case Tier B exposes issues.
- **Contribution-weight accumulation across runs.** The
  `evolve-dag-accumulate!` flag exists but its semantics under topology
  mutations (where contribution weights attach to concepts that may
  be deleted) are undefined. Resolve in 5c (remove) or 5d (duplicate)
  before shipping Tier B.
- **Recursive concepts.** Tier A detects them during DAG build and
  marks them opaque. Topology mutations (insert especially) must not
  create new cycles. 5a's cycle-check requirement is called out above.

---

## Notes on Process

This plan is deliberately more conservative than the original. Three
reasons:

1. The original plan was written before Tier A's bugs were exposed. It
   assumed a level of ground-truth that turned out not to hold.
2. Topology mutations are genuinely more invasive than AST mutations —
   they modify the dictionary's identity, not just impl internals.
   Mistakes compound harder.
3. A failed topology mutation isn't just "bad fitness" — it can
   corrupt the DAG and all subsequent evolution. The per-phase
   validation gates exist because we cannot afford a Phase 4–style
   discovery four weeks later that the whole phase has been silently
   broken.

If at any P-phase or sub-phase the validation fails, **stop and decide
whether to redesign rather than patch.** Tier A's compounded bugs came
from patching past symptoms instead of stopping to diagnose the root
cause; Tier B should not repeat that pattern.
