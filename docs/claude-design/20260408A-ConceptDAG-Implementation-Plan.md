# ConceptDAG — Implementation Plan

**Date:** 2026-04-08
**Design:** `20260407D-ConceptDAG-Design.md` (with question responses)
**Prerequisites:** v2.2.2 (MCE Phase 1a/1b shipped, `evolve-sub` / `evolve-chain` / `evolve-seed!` operational)
**Status:** Plan
**Workflow:** See `docs/claude-knowledge/20260403A-feature-branch-workflow.md`

---

## Context: Where This Plan Fits

ETIL's evolution engine operates at two levels. The **micro level** (AST) evolves individual word implementations via genetic operators — substitute, perturb, grow, shrink, crossover. This has been operational since v1.12.10 (Type-Directed Bridges) and produces working mutations within a single word's bytecode.

The **macro level** is the concept call graph: words call words that call words. Until now, this structure was invisible to the evolution engine. MCE Phases 1a/1b (`20260405D`, `20260407A/B`) added `evolve-sub` and `evolve-chain` to evolve sub-concepts within a user-defined chain, but the chain was flat (round-robin scheduling) and the topology was frozen.

The ConceptDAG design (`20260407D-ConceptDAG-Design.md`) replaces the flat chain model with a nested DAG that makes the call graph explicit and — in Tier B — evolvable. It unifies the original MCE plan's Phases 2–4:

| Original MCE Phase | ConceptDAG Equivalent |
|--------------------|-----------------------|
| Phase 2 — co-evolutionary credit | Contribution weights computed from fitness variance |
| Phase 3 — module-boundary crossover | Sub-DAG transplant between chains (Tier B) |
| Phase 4 — evolved chain structure | DAG topology mutations: insert, remove, duplicate, absorb (Tier B) |

The key architectural insight: the concept call graph alternates **Concept → impl → Concept → impl**. Concept nodes carry contribution weight (how much they matter to chain fitness). Impl nodes carry fitness weight (which bytecode gets selected). The `BridgeMap` provides type safety at both levels — AST-level type repair and DAG-level edge validation use the same bridge infrastructure.

This plan is the implementation roadmap for that design.

---

## Overview

The ConceptDAG implementation is split into two tiers:

**Tier A (Phases 0–4): Foundation.** The ConceptDAG data structure, contribution-weighted scheduling, variance-based credit, per-node statistics, logging, and TIL query words. This tier replaces the flat `evolve-chain` round-robin with a nested, credit-aware evolution scheduler. No topology mutations yet — the DAG structure is built from the user's decomposition and stays fixed during evolution.

**Tier B (Phases 5–6): Topology Evolution.** DAG-level mutations (insert, remove, duplicate, absorb) and subtree operations (crossover, duplication). This tier makes the DAG structure itself evolvable. Depends on Tier A being validated.

| Phase | What | Where | Est. Effort |
|-------|------|-------|-------------|
| **0** | ConceptDAG data structure + eager extraction | C++ (new files) | ~1 day |
| **1** | DAG-aware evolution scheduling | C++ (EvolutionEngine) | ~2 days |
| **2** | Contribution weight computation + statistics | C++ (EvolutionEngine) | ~1 day |
| **3** | Logging, TIL words, evolve-dag-show | C++ (primitives, logger) | ~1 day |
| **4** | Integration test + benchmark | TIL + C++ tests | ~1 day |
| **5** | DAG topology mutations | C++ (new DAGGeneticOps) | ~3 days |
| **6** | Subtree operations + bloat control | C++ (DAGGeneticOps) | ~2 days |

**Branch:** `scripts/branch.sh concept-dag` (creates v2.3.0, Tier A phases bump patch)

---

## Design Decisions (from question responses)

These decisions are locked and shape every phase:

| Decision | Choice | Implication |
|----------|--------|-------------|
| DAG extraction | **Eager** | Build DAG at `evolve-dag-register` time, not lazily |
| Contribution weight persistence | **Reset per run**, boolean toggle for accumulation | Default ephemeral; `evolve-dag-accumulate!` word for cross-run carry |
| Recursive concepts | **Forbid** — treat as opaque leaves | Cycle detection during extraction; recursive words excluded from DAG scheduling |
| Multi-root interaction | **Per-root** with gene duplication for specialization | Each root has its own contribution weight map; shared concepts get duplicated if roots diverge |
| Absorb granularity | **Permanent** | No undo; absorbed concept is erased from DAG |
| Implementation groups | **Impl-scoped** pools only; DAG topology pools deferred | `evolve-register-pool` applies to AST mutations within impls, not to DAG node insertion |
| Per-node statistics | **Required** | Each ConceptDAG node tracks fitness stats; logged at end of evolution and optionally mid-run |

---

## Phase 0 — ConceptDAG Data Structure

**Goal:** Define the ConceptDAG as a standalone data structure that can be built from the dictionary call graph. No evolution logic yet — just the graph.

**Version:** v2.3.1

### New Files

**`include/etil/evolution/concept_dag.hpp`**

```cpp
namespace etil::evolution {

/// Statistics tracked per concept node across generations.
struct ConceptNodeStats {
    size_t generations_evolved = 0;
    size_t children_created = 0;
    size_t children_pruned = 0;
    double best_fitness = 0.0;
    double worst_fitness = 1.0;
    double mean_fitness = 0.0;
    double fitness_variance = 0.0;  // used for contribution weight computation
    size_t impl_count = 0;         // current number of impls in dictionary
};

/// A node in the ConceptDAG. Represents a word concept at a specific
/// position in the call graph relative to a root.
struct ConceptDAGNode {
    std::string name;                          // concept name (dictionary key)
    double contribution = 1.0;                 // Tier 2 weight: evolution scheduling priority
    size_t depth = 0;                          // distance from root
    bool opaque = false;                       // true for primitives and recursive concepts
    TypeSignature type_contract;               // expected stack effect
    ConceptNodeStats stats;
    std::vector<std::string> children;         // child concept names (from Call instructions)
    std::vector<std::string> parent_impls;     // which parent impls reference this concept
};

/// The ConceptDAG: an explicit, inspectable representation of the concept
/// call graph rooted at one or more entry points. Built eagerly from
/// bytecode Call instructions at registration time.
class ConceptDAG {
public:
    /// Build the DAG by scanning bytecode of root_concept and all reachable
    /// concepts. Recursive concepts are marked opaque. Primitives are leaves.
    void build(const std::string& root_concept,
               etil::core::Dictionary& dict);

    /// Get a node by concept name. Returns nullptr if not in DAG.
    ConceptDAGNode* node(const std::string& name);
    const ConceptDAGNode* node(const std::string& name) const;

    /// Get all concept names in the DAG (topological order, root first).
    const std::vector<std::string>& topo_order() const;

    /// Get the root concept name.
    const std::string& root() const;

    /// Get all non-opaque, non-root concept names (evolvable targets).
    std::vector<std::string> evolvable_concepts() const;

    /// Select a concept for evolution, weighted by contribution × depth_discount^depth.
    std::string select_for_evolution(std::mt19937_64& rng,
                                     double depth_discount = 1.0) const;

    /// Reset all contribution weights and statistics (called at start of run).
    void reset();

    /// Print DAG structure with weights to an output stream.
    void dump(std::ostream& out) const;

private:
    std::string root_;
    std::vector<std::string> topo_order_;      // topological sort (root first)
    std::unordered_map<std::string, ConceptDAGNode> nodes_;

    void scan_impl(const std::string& parent_concept,
                   const etil::core::WordImpl& impl,
                   etil::core::Dictionary& dict,
                   std::unordered_set<std::string>& visited);
};

} // namespace etil::evolution
```

**`src/evolution/concept_dag.cpp`**

Implements `build()`:
1. Start with root concept. Add as node with depth=0.
2. For each impl of the root, scan bytecode for `Call` instructions.
3. Each `Call` target is a child concept. Add as node with depth=parent+1.
4. If the child is a primitive (native_code, no bytecode), mark opaque.
5. If the child was already visited (cycle), mark opaque.
6. Recurse into non-opaque children.
7. Compute topological order via DFS post-order reversal.

### Implementation Checklist

- [ ] Create `include/etil/evolution/concept_dag.hpp` with structs and class declaration
- [ ] Create `src/evolution/concept_dag.cpp` with `build()`, `node()`, `topo_order()`, `evolvable_concepts()`, `select_for_evolution()`, `reset()`, `dump()`
- [ ] Add `concept_dag.cpp` to `src/CMakeLists.txt`
- [ ] Unit tests in `tests/unit/test_concept_dag.cpp`:
    - `BuildSimpleChain` — linear A→B→C, verify 3 nodes, correct depths, topo order
    - `BuildDiamond` — A→B, A→C, B→D, C→D, verify D appears once at depth 2
    - `DetectRecursion` — A→B→A, verify A is opaque (not re-entered)
    - `PrimitivesAreOpaque` — A calls `+` (native), verify `+` is opaque
    - `EmptyConcept` — root has no impls, verify empty DAG
    - `SelectForEvolution` — verify weighted selection respects contribution and depth
    - `DumpFormat` — verify `dump()` output contains expected nodes and weights

### Validation

Phase 0 passes if all unit tests pass and `dump()` produces a readable tree for the quadratic benchmark decomposition (`target-fn → square-term, linear-term, offset`).

---

## Phase 1 — DAG-Aware Evolution Scheduling

**Goal:** Replace `evolve-chain`'s flat round-robin with contribution-weighted scheduling that operates on the ConceptDAG. Evolution still uses `evolve_sub_concept()` under the hood.

**Version:** v2.3.2

### Changes to EvolutionEngine

Add a `ConceptDAG` member and a new method:

```cpp
// evolution_engine.hpp
class EvolutionEngine {
    // ... existing members ...
    std::unordered_map<std::string, ConceptDAG> dags_;  // per-root DAGs
    bool accumulate_contributions_ = false;

public:
    /// Register a root concept and build its ConceptDAG from the call graph.
    bool register_dag(const std::string& root_concept,
                      std::vector<TestCase> tests);

    /// Run one generation of DAG-aware evolution:
    /// 1. Select concept C from DAG weighted by contribution
    /// 2. Call evolve_sub_concept(C, root) for AST-level mutation
    /// 3. Update stats on C's node
    size_t evolve_dag_generation(const std::string& root_concept);

    /// Run N generations of DAG-aware evolution.
    void evolve_dag(const std::string& root_concept, size_t generations);

    /// Get the ConceptDAG for a root (nullptr if not registered).
    ConceptDAG* dag(const std::string& root_concept);
    const ConceptDAG* dag(const std::string& root_concept) const;

    /// Toggle contribution weight accumulation across runs.
    void set_accumulate_contributions(bool flag);
};
```

### Implementation Details

`register_dag()`:
1. Store tests in `word_state_[root_concept]` (reuses existing test registration).
2. Build `ConceptDAG` via `dag.build(root_concept, dict_)`.
3. Store in `dags_[root_concept]`.
4. If `!accumulate_contributions_`, call `dag.reset()`.

`evolve_dag_generation()`:
1. Look up DAG for root.
2. Call `dag.select_for_evolution(rng_, depth_discount)` to pick a concept C.
3. Call existing `evolve_sub_concept(C, root_concept)`.
4. Update `C.stats` with results (generations_evolved++, fitness tracking).
5. Return children_created.

`evolve_dag()`:
1. Loop `generations` times, calling `evolve_dag_generation()`.

### New TIL Primitives

```cpp
// evolve-dag-register ( root-str tests-array -- flag )
bool prim_evolve_dag_register(ExecutionContext& ctx);

// evolve-dag ( root-str generations -- )
bool prim_evolve_dag(ExecutionContext& ctx);

// evolve-dag-accumulate! ( flag -- )
bool prim_evolve_dag_accumulate(ExecutionContext& ctx);
```

### Help Entries

Add to `data/help.til` for `evolve-dag-register`, `evolve-dag`, `evolve-dag-accumulate!`.

### Implementation Checklist

- [ ] Add `ConceptDAG` member and new methods to `EvolutionEngine`
- [ ] Implement `register_dag()`, `evolve_dag_generation()`, `evolve_dag()`
- [ ] Add `prim_evolve_dag_register`, `prim_evolve_dag`, `prim_evolve_dag_accumulate` to `primitives.cpp`
- [ ] Register primitives in PRIMS table, update primitive count test
- [ ] Add help entries
- [ ] Unit tests:
    - `RegisterDAGBuildsGraph` — register root, verify DAG is built with correct nodes
    - `EvolveDagGenerationSelectsConcept` — verify evolve_dag_generation calls evolve_sub_concept
    - `EvolveDagRunsNGenerations` — verify generation count matches
- [ ] TIL integration test: `test_concept_dag.til`
    - Define decomposed chain, register DAG, run 5 generations, verify sub-concept evolve-status > 0

### Validation

Phase 1 passes if the quadratic benchmark decomposition can be registered as a DAG and evolved via `evolve-dag` with contribution-weighted scheduling, producing varied sub-concept generation counts (not uniform round-robin).

---

## Phase 2 — Contribution Weight Computation + Statistics

**Goal:** Compute contribution weights using variance-based credit (Option B from design). Track per-node statistics.

**Version:** v2.3.3

### Variance-Based Contribution Weight

After each generation where concept C was evolved, update C's contribution weight:

```
For concept C in the DAG:
    Run K evaluations of the root chain with different impl selections for C
    (use the SelectionEngine's weighted-random to sample K different impls)
    Compute fitness variance across K evaluations
    C.stats.fitness_variance = variance
    C.contribution = normalize(variance) across all evolvable concepts
```

K is configurable (default 5). The cost is K extra fitness evaluations per generation.

Normalization ensures contributions sum to 1.0 across all evolvable concepts in the DAG.

### EvolutionConfig Additions

```cpp
struct EvolutionConfig {
    // ... existing ...
    size_t dag_variance_k = 5;       // evaluations per concept for variance computation
    double dag_depth_discount = 1.0; // depth attenuation for scheduling (1.0 = disabled)
};
```

### ConceptNodeStats Updates

After each generation of concept C:
```cpp
node.stats.generations_evolved++;
node.stats.children_created += children;
node.stats.impl_count = dict_.get_implementations(name)->size();
// Update best/worst/mean fitness from the generation's results
// Update fitness_variance from K-sample variance computation
```

### Implementation Checklist

- [ ] Add `dag_variance_k` and `dag_depth_discount` to `EvolutionConfig`
- [ ] Implement variance-based contribution weight update in `evolve_dag_generation()`
- [ ] Update `ConceptNodeStats` after each generation
- [ ] Add `prim_evolve_contribution` — `evolve-contribution ( concept-str -- x )` to query contribution weight
- [ ] Add `prim_evolve_dag_variance_k` — `evolve-dag-variance-k! ( n -- )` to set K
- [ ] TIL words for `dag_depth_discount` — `evolve-dag-depth-discount! ( x -- )`
- [ ] Register primitives, update count test, add help entries
- [ ] Unit tests:
    - `VarianceComputationNonZero` — concept with multiple impls has nonzero variance
    - `ContributionWeightsNormalize` — all contributions sum to 1.0
    - `HighVarianceConceptEvolvesMore` — over N generations, high-contribution concept gets more evolve calls

### Validation

Phase 2 passes if, on the quadratic benchmark, contribution weights differentiate sub-concepts: `square-term` and `linear-term` should have higher contribution than `offset` (since `offset` is a constant with no mutation value).

---

## Phase 3 — Logging, TIL Query Words, evolve-dag-show

**Goal:** Add DAG-specific logging category, per-node stats output, and the `evolve-dag-show` word for interactive inspection.

**Version:** v2.3.4

### EvolveLogCategory Addition

```cpp
DAG = 1 << 16    // ConceptDAG scheduling, contribution weight updates, topology changes
```

Update `All` mask to include bit 16.

### Logging Points

| When | What | Category |
|------|------|----------|
| `register_dag()` | DAG structure (node count, depth, opaque nodes) | DAG |
| `evolve_dag_generation()` start | Selected concept, contribution weight, depth | DAG |
| Contribution weight update | Old weight → new weight, variance | DAG |
| End of `evolve_dag()` | Full stats summary per node | DAG |
| Mid-evolution (optional) | Stats snapshot every N generations | DAG |

### Mid-Evolution Stats Frequency

Add config:
```cpp
size_t dag_stats_interval = 0;  // 0 = end-only, >0 = every N generations
```

TIL word: `evolve-dag-stats-interval! ( n -- )`

### New TIL Primitives

```cpp
// evolve-dag-show ( root-str -- )
// Print DAG structure with contribution weights, depths, and stats.
bool prim_evolve_dag_show(ExecutionContext& ctx);
```

Output format:
```
ConceptDAG: target-fn (3 concepts, depth 1)
  target-fn        [root]  depth=0  contrib=1.000  gens=0   impls=2  best=0.999
    square-term    depth=1  contrib=0.420  gens=15  impls=4  best=0.998  var=0.032
    linear-term    depth=1  contrib=0.355  gens=12  impls=3  best=0.997  var=0.018
    offset         depth=1  contrib=0.225  gens=3   impls=1  best=0.999  var=0.001
```

### Implementation Checklist

- [ ] Add `DAG = 1 << 16` to `EvolveLogCategory`
- [ ] Add `dag_stats_interval` to `EvolutionConfig`
- [ ] Add logging to `register_dag()`, `evolve_dag_generation()`, `evolve_dag()`
- [ ] Implement end-of-evolution stats dump
- [ ] Implement optional mid-evolution stats logging
- [ ] Add `prim_evolve_dag_show`, `prim_evolve_dag_stats_interval`
- [ ] Register primitives, update count test, add help entries
- [ ] Unit tests:
    - `DAGLogCategoryEnabled` — verify DAG category can be enabled independently
    - `EndOfEvolutionStatsDumped` — run evolve-dag, verify stats in log
    - `MidEvolutionStatsInterval` — set interval=5, verify stats appear at gen 5, 10, 15

### Validation

Phase 3 passes if `evolve-dag-show` produces readable output for the quadratic benchmark after evolution, and the evolution log contains DAG category entries.

---

## Phase 4 — Integration Test and Benchmark

**Goal:** End-to-end validation of the ConceptDAG foundation (Tier A). Compare DAG-aware evolution against flat `evolve-chain` and monolithic `evolve-word`.

**Version:** v2.3.5

### New Test File

**`tests/til/test_concept_dag.til`** — integration test:
- Define quadratic decomposition
- Register via `evolve-dag-register`
- Run 30 generations via `evolve-dag`
- Verify sub-concepts have non-uniform generation counts
- Verify `evolve-contribution` returns non-zero for all concepts
- Verify `evolve-dag-show` produces output
- 10+ assertions

**`tests/til/test_concept_dag.sh`** — shell wrapper (same pattern as `test_mce.sh`)

### New Benchmark

**`tests/til/bench/bench_dag_quad.til`** — DAG-aware quadratic benchmark:
- Same target as `bench_mce_quad.til` but uses `evolve-dag` instead of `evolve-chain`
- Logs DAG stats at end
- Same seed / generation count for comparison

### Updated Validation Script

Update `tests/til/bench/validate_mce.sh` to include a fourth column: MCE-DAG.

### CTest Registration

Add `test_concept_dag` to CTest via `tests/til/CMakeLists.txt` (or however TIL tests are registered).

### Documentation

- [ ] Update `README.md`: add ConceptDAG section to evolution appendix, add new words to word table
- [ ] Update `data/help.til`: entries for all new words
- [ ] CLAUDE.md is .gitignored so update locally only

### Implementation Checklist

- [ ] Create `test_concept_dag.til` and `test_concept_dag.sh`
- [ ] Create `bench_dag_quad.til`
- [ ] Update `validate_mce.sh` with DAG column
- [ ] Register test in CTest
- [ ] Update README.md with ConceptDAG section and word table
- [ ] Build all, test all
- [ ] Run validation: compare monolithic vs evolve-chain vs evolve-dag across 5 seeds

### Validation

Phase 4 passes if:
1. All existing tests still pass (no regression)
2. `test_concept_dag.til` passes
3. DAG evolution produces non-uniform sub-concept scheduling (contribution-weighted, not round-robin)
4. `evolve-dag-show` produces correct output
5. Benchmark results are documented

### Tier A Super-Push Gate

After Phase 4, the branch is ready for super-push if all tests pass and benchmark results are documented. Tier A provides the foundation; Tier B (topology evolution) ships on a subsequent branch.

---

## Phase 5 — DAG Topology Mutations (Tier B)

**Goal:** Make the DAG structure itself evolvable. Add node-level mutations that modify the concept graph.

**Version:** v2.4.1 (new branch: `scripts/branch.sh concept-dag-topology`)

### New File: `DAGGeneticOps`

**`include/etil/evolution/dag_genetic_ops.hpp`**
**`src/evolution/dag_genetic_ops.cpp`**

```cpp
class DAGGeneticOps {
public:
    DAGGeneticOps(etil::core::Dictionary& dict, BridgeMap& bridge_map);

    /// Insert a new concept node between parent_impl and child_concept.
    /// The new concept is created in the dictionary with a bridge-derived impl.
    /// Returns true if insertion succeeded (type-bridgeable).
    bool insert_node(ConceptDAG& dag,
                     const std::string& parent_concept,
                     const std::string& child_concept,
                     std::mt19937_64& rng);

    /// Remove a concept node from the DAG. Rewires parent to call child's children.
    /// Returns true if removal succeeded (type-compatible bypass).
    bool remove_node(ConceptDAG& dag,
                     const std::string& concept_to_remove);

    /// Duplicate a concept node, creating a new concept with a copy of the
    /// original's impl population. The new concept replaces one reference
    /// to the original in a parent impl.
    bool duplicate_node(ConceptDAG& dag,
                        const std::string& concept_to_duplicate,
                        std::mt19937_64& rng);

    /// Absorb a child concept into its parent by inlining the child's best
    /// impl into the parent's bytecode. Permanent — child concept is removed.
    bool absorb_node(ConceptDAG& dag,
                     const std::string& parent_concept,
                     const std::string& child_to_absorb);
};
```

### Mutation Details

**Insert:**
1. Pick a random edge (parent→child) in the DAG.
2. Query `BridgeMap` for a concept that can bridge parent's output type to child's input type.
3. Create the new concept in the dictionary with a bridge-word impl.
4. Rewrite parent's impl bytecode: replace `Call child` with `Call new_concept`.
5. New concept's impl calls child.
6. Rebuild DAG.

**Remove:**
1. Pick a concept C with contribution weight below a threshold.
2. Verify C's parent output type is compatible with C's children's input types (directly or via bridge).
3. Rewrite parent's impl bytecode: replace `Call C` with calls to C's children (or inline C's best impl).
4. Remove C from dictionary (`forget_all`).
5. Rebuild DAG.

**Duplicate (Gene Duplication):**
1. Pick a concept C with contribution weight above a threshold (working hard, candidate for specialization).
2. Create `C-dup-N` in dictionary with copies of C's impls.
3. Pick one parent impl that calls C. Replace one `Call C` with `Call C-dup-N`.
4. Both C and C-dup-N now evolve independently.
5. Rebuild DAG.

**Absorb:**
1. Pick a concept C with exactly one parent and one impl.
2. Inline C's bytecode into the parent impl's bytecode at the Call site.
3. `forget_all(C)`.
4. Rebuild DAG.

### Integration with Evolution Scheduling

In `evolve_dag_generation()`, with configurable probability:
- Instead of AST-level mutation, perform a DAG-level topology mutation.
- Config: `double dag_mutation_rate = 0.05` (5% of generations are topology mutations).

### Implementation Checklist

- [ ] Create `dag_genetic_ops.hpp` and `dag_genetic_ops.cpp`
- [ ] Implement `insert_node()`, `remove_node()`, `duplicate_node()`, `absorb_node()`
- [ ] Add `dag_mutation_rate` to `EvolutionConfig`
- [ ] Integrate topology mutations into `evolve_dag_generation()`
- [ ] Add DAGGeneticOps member to EvolutionEngine
- [ ] Unit tests:
    - `InsertNodeBridgeable` — insert between compatible types succeeds
    - `InsertNodeIncompatible` — insert between unbridgeable types fails gracefully
    - `RemoveNodeRewires` — remove node, verify parent calls children directly
    - `DuplicateNodeCreatesIndependentCopy` — verify both original and dup exist, diverge
    - `AbsorbNodeInlines` — verify child's bytecode appears in parent, child removed
    - `AbsorbIsPermanent` — verify absorbed concept is gone from dictionary
- [ ] TIL integration test: topology mutations don't crash on quadratic benchmark

---

## Phase 6 — Subtree Operations and Bloat Control (Tier B)

**Goal:** Add sub-DAG level operations and limits to prevent unbounded DAG growth.

**Version:** v2.4.2

### Subtree Operations

```cpp
// In DAGGeneticOps:

/// Transplant a sub-DAG from source_root into target_dag at a type-compatible point.
bool subtree_crossover(ConceptDAG& target_dag,
                       const ConceptDAG& source_dag,
                       std::mt19937_64& rng);

/// Duplicate an entire sub-DAG rooted at a concept, creating parallel specialization.
bool subtree_duplicate(ConceptDAG& dag,
                       const std::string& subtree_root,
                       std::mt19937_64& rng);
```

### Bloat Control

Add to `EvolutionConfig`:
```cpp
size_t max_dag_depth = 5;    // maximum nesting depth
size_t max_dag_nodes = 20;   // maximum concept count in one DAG
```

Enforced in:
- `insert_node()`: reject if depth would exceed max
- `duplicate_node()`: reject if node count would exceed max
- `subtree_duplicate()`: reject if resulting DAG exceeds limits
- `evolve_dag_generation()`: if DAG exceeds limits, force an absorb or remove mutation

### Implementation Checklist

- [ ] Implement `subtree_crossover()` and `subtree_duplicate()`
- [ ] Add `max_dag_depth` and `max_dag_nodes` to `EvolutionConfig`
- [ ] Enforce limits in all DAG mutation methods
- [ ] Add TIL words for limits: `evolve-dag-max-depth!`, `evolve-dag-max-depth@`, `evolve-dag-max-nodes!`, `evolve-dag-max-nodes@`
- [ ] Unit tests:
    - `BloatControlRejectsDeepInsert` — insert at max depth fails
    - `BloatControlRejectsOversize` — duplicate when at max nodes fails
    - `SubtreeCrossoverTypeCompatible` — transplant between compatible DAGs succeeds
    - `SubtreeCrossoverIncompatible` — transplant between incompatible types fails
- [ ] Integration test with bloat control: DAG stays within limits over 100 generations

### Tier B Super-Push Gate

Tier B ships if topology mutations produce measurable benefits on a benchmark with 10+ instruction sub-concepts. If not, Tier B is held and documented.

---

## New TIL Words Summary (All Phases)

| Word | Stack Effect | Phase | Description |
|------|-------------|-------|-------------|
| `evolve-dag-register` | `( root-str tests-array -- flag )` | 1 | Build ConceptDAG from call graph, register tests |
| `evolve-dag` | `( root-str generations -- )` | 1 | Run DAG-aware contribution-weighted evolution |
| `evolve-dag-accumulate!` | `( flag -- )` | 1 | Toggle contribution weight carry across runs |
| `evolve-contribution` | `( concept-str -- x )` | 2 | Query a concept's contribution weight |
| `evolve-dag-variance-k!` | `( n -- )` | 2 | Set K for variance-based contribution |
| `evolve-dag-depth-discount!` | `( x -- )` | 2 | Set depth attenuation factor |
| `evolve-dag-show` | `( root-str -- )` | 3 | Print DAG structure with weights and stats |
| `evolve-dag-stats-interval!` | `( n -- )` | 3 | Set mid-evolution stats logging frequency |
| `evolve-dag-max-depth!` | `( n -- )` | 6 | Set maximum DAG nesting depth |
| `evolve-dag-max-depth@` | `( -- n )` | 6 | Get maximum DAG nesting depth |
| `evolve-dag-max-nodes!` | `( n -- )` | 6 | Set maximum concept count per DAG |
| `evolve-dag-max-nodes@` | `( -- n )` | 6 | Get maximum concept count per DAG |

---

## File Inventory (New Files)

| File | Phase | Purpose |
|------|-------|---------|
| `include/etil/evolution/concept_dag.hpp` | 0 | ConceptDAG class + node structs |
| `src/evolution/concept_dag.cpp` | 0 | DAG build, query, dump |
| `tests/unit/test_concept_dag.cpp` | 0 | Unit tests for data structure |
| `tests/til/test_concept_dag.til` | 4 | TIL integration test |
| `tests/til/test_concept_dag.sh` | 4 | Shell wrapper |
| `tests/til/bench/bench_dag_quad.til` | 4 | DAG benchmark |
| `include/etil/evolution/dag_genetic_ops.hpp` | 5 | Topology mutation operators |
| `src/evolution/dag_genetic_ops.cpp` | 5 | Topology mutation implementation |

---

## Risk Assessment

| Risk | Phase | Mitigation |
|------|-------|------------|
| Eager extraction misses dynamically defined concepts | 0 | Document: only concepts defined before `evolve-dag-register` are included |
| Contribution-weighted scheduling starves low-contribution concepts | 1 | Minimum contribution floor (like `prune_threshold`) ensures all concepts get some evolution |
| Variance computation (K evaluations) is expensive | 2 | Default K=5; tunable via `evolve-dag-variance-k!`; can be reduced to K=1 (disabling variance) |
| DAG rebuild after topology mutation is expensive | 5 | Incremental DAG update (modify edges) instead of full rebuild — optimize if profiling shows hot |
| Gene duplication creates name collisions | 5 | Naming scheme: `concept-dup-N` with incrementing counter stored in ConceptDAG |
| Absorb permanently loses modularity | 5 | By design (decision: permanent). User can re-decompose manually if needed |
| Benchmark sub-concepts still too simple for Tier B | 5 | Design a 10+ instruction benchmark before implementing Tier B |
