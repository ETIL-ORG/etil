# ConceptDAG — Nested Modular Co-Evolution

**Date:** 2026-04-07
**Status:** Design
**Predecessors:**
- `20260325A-Modular-Coevolution-Design.md` — original MCE design
- `20260405D-MCE-Phases-1-3-Implementation-Plan.md` — linear chain phases
- `20260407A/B/C` — Phase 1a/1b validation and experiments
- `20260403B-Type-Directed-Bridges-Implementation-Plan.md` — TDB (load-bearing)

---

## Motivation

MCE Phases 1a/1b built the infrastructure for modular co-evolution: `evolve-sub`, `evolve-chain`, `evolve-seed!`, dictionary cache invalidation, fitness stdout capture. Phase 1b's experiments showed that MCE works mechanically but doesn't outperform monolithic evolution on benchmarks with simple sub-concepts.

The experiments identified two structural limitations:

1. **Linear chain model.** `evolve-chain` treats sub-concepts as a flat list and evolves them round-robin. Real programs are nested — words call words that call words. The call graph is a DAG, not a pipeline.

2. **No topology evolution.** The user fixes the decomposition. MCE can evolve implementations within each sub-concept but cannot add, remove, reorder, or duplicate sub-concepts. The chain structure is frozen at registration time.

Phases 2 (mean-over-partners credit) and 3 (module-boundary crossover) were designed as refinements to the linear chain model. But the validation data shows the bottleneck is not credit assignment or crossover — it's that the fixed chain topology constrains the search space. Sub-concepts that are too simple can't benefit from evolution; sub-concepts that are too complex can't be evolved without structural decomposition.

**ConceptDAG replaces Phases 2-4** with a unified architecture that makes the concept call graph an explicit, evolvable structure.

---

## Architecture

### The DAG Structure

The ConceptDAG alternates between two node types:

```
[root] → Concept → impl → Concept → impl → ...
```

- **Root** — an array of active root Concepts (entry points for evolution)
- **Concept nodes** — abstract word concepts from the dictionary. Internal nodes. Carry contribution weight and type contract.
- **Impl nodes** — concrete implementations (bytecode). Terminal nodes. Carry fitness weight. Their bytecode contains `Call` instructions to child Concept nodes.

```
[root: target-fn, other-fn, ...]
  │
  └── Concept: target-fn
        │   contribution: 1.0
        │   type: (Integer → Integer)
        │
        ├── impl#1 [w=0.85] "dup square-term swap linear-term + offset +"
        │     │
        │     ├── Concept: square-term
        │     │     type: (Integer → Integer)
        │     │     contribution: 0.4
        │     │     ├── impl#1 [w=0.9] "dup *"
        │     │     └── impl#2 [w=0.3] "dup dup * nip"
        │     │
        │     ├── Concept: linear-term
        │     │     type: (Integer → Integer)
        │     │     contribution: 0.35
        │     │     └── impl#1 [w=1.0] "3 *"
        │     │
        │     └── Concept: offset
        │           type: ( → Integer)
        │           contribution: 0.25
        │           └── impl#1 [w=1.0] "5"
        │
        └── impl#2 [w=0.70] "dup dup * swap 3 * + 5 +"
              │
              └── (no child concepts — monolithic impl)
```

### Existing Infrastructure Mapping

The ConceptDAG does not replace the dictionary — it **annotates** it.

| Component | Already exists | ConceptDAG adds |
|-----------|---------------|-----------------|
| Concept → impls | `Dictionary::concepts_` | Contribution weight per concept |
| Impl → child concepts | `Call` instructions in bytecode | Explicit edge list, extracted from bytecode |
| Impl selection | `SelectionEngine` (weighted-random) | Unchanged |
| Type contracts | `TypeSignature` on each word | Edge type contracts between parent impl and child concept |
| Type bridging | `BridgeMap` + `TypeRepair` | Bridge insertion at concept edges, not just AST nodes |
| Fitness evaluation | `Fitness::evaluate()` | Hierarchical: evaluate root, propagate credit down DAG |

The key addition is making the implicit call graph (embedded in bytecode `Call` instructions) into an explicit, inspectable, evolvable data structure.

---

## Two Tiers of Weight

### Tier 1: Impl Weight (existing)

Controls which implementation is selected for a concept. Managed by the existing evolution engine's `update_weights()` and `prune()`. Updated per-generation based on fitness evaluation.

```
Concept: square-term
  impl#1 "dup *"        w=0.90  ← selected ~90% of the time
  impl#2 "dup dup * nip" w=0.10  ← selected ~10%, candidate for pruning
```

### Tier 2: Contribution Weight (new)

Controls how much a concept node contributes to its parent chain's fitness. This is the credit-assignment mechanism that Phases 1b and 2 attempted to solve:

```
Concept: target-fn
  └── impl#1 calls:
        square-term   contribution=0.40  ← high: mutations here affect fitness most
        linear-term   contribution=0.35  ← moderate
        offset        contribution=0.25  ← low: nearly constant, little mutation value
```

Contribution weight determines:
- **Evolution scheduling** — concepts with higher contribution get more evolution cycles
- **Gene duplication trigger** — contribution > threshold suggests the concept is doing too much work; split it
- **Pruning trigger** — contribution → 0 suggests the concept is redundant; absorb it into the parent

### How Contribution Weight Is Computed

**Option A: Ablation-based.** Remove the concept's contribution (replace with identity or bridge-to-default) and measure fitness drop. `contribution = fitness_with - fitness_without`. Expensive (requires re-evaluation per concept) but direct.

**Option B: Variance-based.** Across N evaluations with different impl selections for this concept, measure how much chain fitness varies. High variance = high contribution (the concept's impl choice matters). Low variance = low contribution (the concept doesn't affect the outcome much).

**Option C: Gradient proxy.** After each generation, compare chain fitness before and after evolving this concept. If fitness changed significantly, contribution is high. This is cheap (no extra evaluations) but noisy.

Default: **Option B** (variance-based). It reuses the existing evaluation infrastructure and provides a clean signal without ablation cost. It also naturally extends to nested DAGs — a concept's variance is measured at whatever level of the DAG it sits.

---

## Type Bridging at Every Edge

The BridgeMap is the shared infrastructure across both evolution tiers:

### AST Level (existing)

When `substitute_call()` replaces a word in an impl's AST, `TypeRepair` inserts bridges to fix type mismatches. The `BridgeMap` provides conversion paths (e.g., `Integer → Float` via `int->float`).

### ConceptDAG Level (new)

When a topology mutation adds a new concept node to the DAG, the edges to its neighbors must be type-compatible. The same `BridgeMap` validates and repairs:

```
Before mutation:
  impl → Concept:square-term (Integer→Integer) → impl → Concept:format (Integer→String)

Topology mutation: insert Concept:normalize between square-term and format
  impl → Concept:square-term (Integer→Integer) → Concept:normalize (Float→Float) → Concept:format (Integer→String)

Type mismatch: square-term outputs Integer, normalize expects Float
Bridge insertion: insert int->float edge between square-term and normalize
Bridge insertion: insert float->int edge between normalize and format
```

The bridge insertion logic at the DAG level mirrors `TypeRepair` at the AST level. The `BridgeMap`'s learned weights (from TBBP) inform which bridges are most likely to produce viable chains.

---

## DAG Topology Mutations

### Node Mutations

| Mutation | Description | Type constraint |
|----------|-------------|----------------|
| **Insert** | Add a new concept node between two existing nodes | Must be type-bridgeable at both edges |
| **Remove** | Remove a concept node, connect its parent directly to its child | Parent output must be type-compatible with child input |
| **Duplicate** | Copy a concept node (and its impl population), creating a specialized variant | Same type as original; impls diverge over time |
| **Absorb** | Inline a child concept's best impl into the parent impl's bytecode | Collapses one level of nesting |

### Edge Mutations

| Mutation | Description |
|----------|-------------|
| **Reorder** | Swap the position of two child concepts within a parent impl |
| **Bridge swap** | Replace the bridge word on an edge with an alternative from the BridgeMap |
| **Contract change** | Widen or narrow the type contract on an edge (e.g., Integer → Unknown) |

### Subtree Mutations

| Mutation | Description |
|----------|-------------|
| **Subtree crossover** | Transplant a sub-DAG from one chain into another at a type-compatible point |
| **Subtree duplication** | Duplicate an entire sub-DAG, creating a parallel branch that can specialize |

---

## Evolution Scheduling

The linear `evolve-chain` round-robin is replaced by contribution-weighted scheduling:

```
for each generation:
    select concept C from DAG weighted by contribution
    evolve C:
        mutate impls of C (AST-level, existing)
        optionally mutate C's sub-DAG topology (DAG-level, new)
    evaluate root concept against test cases
    propagate fitness: update impl weights at C, update contribution weights along path to root
```

Concepts with higher contribution get evolved more frequently. Concepts with near-zero contribution are candidates for pruning. This naturally focuses evolution effort where it matters most.

### Depth-Aware Scheduling

Deeper nodes in the DAG have smaller effect on root fitness (their contribution is attenuated by each intervening level). Scheduling should account for depth:

```
effective_priority = contribution × depth_discount^depth
```

This prevents the scheduler from wasting cycles on deeply nested concepts that barely affect the root. The `depth_discount` (e.g., 0.8) is a tuning parameter.

---

## Relationship to Existing Code

### What changes

| Component | Change |
|-----------|--------|
| `EvolutionEngine` | Add `ConceptDAG` member, DAG-aware scheduling, contribution weight tracking |
| `evolve_sub_concept()` | Generalize to operate on any DAG node, not just a flat sub-concept list |
| `Fitness::evaluate()` | Propagate results back through DAG path for contribution updates |
| `BridgeMap` | Add DAG-edge bridge insertion (reuse existing path-finding logic) |

### What stays the same

| Component | Reason |
|-----------|--------|
| `Dictionary` | ConceptDAG annotates it, doesn't replace it. Concepts and impls stay in the dictionary. |
| `ASTGeneticOps` | Impl-level mutation is unchanged. AST mutations operate within a single impl. |
| `TypeRepair` | AST-level type repair is unchanged. DAG-level bridge insertion is a new user of the same BridgeMap. |
| `SelectionEngine` | Impl selection within a concept is unchanged. |
| `EvolveLogger` | Add DAG-specific log categories but reuse the logging infrastructure. |

### New TIL words

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `evolve-dag-register` | `( root-str tests -- flag )` | Register a root concept and build its ConceptDAG from the call graph |
| `evolve-dag` | `( root-str generations -- )` | Run DAG-aware evolution with contribution-weighted scheduling |
| `evolve-dag-show` | `( root-str -- )` | Print the ConceptDAG structure with weights |
| `evolve-contribution` | `( concept-str -- x )` | Query a concept's contribution weight |

---

## The Dualism

The ConceptDAG makes explicit a dualism that already exists in ETIL:

| | Micro (AST) | Macro (ConceptDAG) |
|---|---|---|
| **Structure** | AST nodes (calls, literals, control flow) | Concept nodes (word references, edges) |
| **Evolution** | ASTGeneticOps (substitute, perturb, grow, shrink) | DAG topology mutations (insert, remove, duplicate, absorb) |
| **Type safety** | TypeRepair + BridgeMap at AST mutation points | BridgeMap at DAG edges |
| **Selection** | Impl weight → which bytecode runs | Contribution weight → which concept gets evolved |
| **Crossover** | Subtree swap within/between ASTs | Sub-DAG transplant between chains |
| **Credit** | Fitness → impl weight update | Fitness variance → contribution weight update |
| **Bloat control** | `max_ast_nodes` limits AST size | `max_dag_depth` / `max_dag_nodes` limits DAG complexity |

The BridgeMap is the shared infrastructure across both levels. TBBP's learned bridge weights inform both AST-level type repair and DAG-level edge validation.

---

## Open Questions

1. **DAG extraction from bytecode.** An impl's child concepts are discovered by scanning its bytecode for `Call` instructions. Should the ConceptDAG be built lazily (scan on first access) or eagerly (scan at registration)? Lazy is simpler but misses concepts added by dynamic word definition.

2. **Contribution weight persistence.** Should contribution weights survive across sessions (stored as concept metadata via `meta!`) or reset per evolution run (like TBBP weights)? Persistent weights allow incremental refinement; ephemeral weights avoid stale signals.

3. **Recursive concepts.** A concept that calls itself (direct recursion) or participates in a cycle (mutual recursion) creates a cycle in the DAG, violating the acyclicity invariant. Options: forbid recursive concepts in the DAG (treat them as opaque leaves), or extend to a concept **graph** (not DAG) with cycle-aware scheduling.

4. **Multi-root interaction.** When multiple root concepts share sub-concepts (e.g., two different chains both use `square-term`), evolving `square-term` for one chain may degrade the other. Should contribution weights be per-root or global? Per-root allows specialization via gene duplication; global enforces shared coherence.

5. **Absorb granularity.** When a concept is absorbed into its parent (inlining), the concept's impls are merged into the parent's bytecode. This is a one-way operation — the concept loses its identity. Should absorption be reversible (mark as absorbed but keep the concept) or permanent?

6. **Interaction with `evolve-register-pool`.** Word pools restrict which words can appear in mutations. Should DAG topology mutations respect the same pool, or have a separate DAG-level pool? A DAG pool would control which concepts can be inserted as nodes.

---

## What This Design Does NOT Cover

- **Automatic decomposition.** The user still provides the initial decomposition (root concept + sub-concepts). ConceptDAG evolves the topology from there but does not discover the initial structure from a monolithic word. That would require AST analysis to identify natural module boundaries — a research problem.

- **Cross-session DAG persistence.** The ConceptDAG is built per evolution run. Persisting evolved topologies across sessions requires serialization of the DAG structure, contribution weights, and impl populations.

- **Visualization.** The `etil-tui` project handles observation. ConceptDAG ships with `evolve-dag-show` for text output and log-based diagnostics, not interactive visualization.

---

## Summary

ConceptDAG makes the implicit concept call graph explicit and evolvable. It unifies Phases 2-4 of the original MCE plan into a single architecture:

- **Phase 2** (credit assignment) → contribution weights computed from fitness variance
- **Phase 3** (module crossover) → sub-DAG transplant between chains
- **Phase 4** (evolved chain structure) → DAG topology mutations with type-safe bridge insertion

The BridgeMap, proven by TDB and TBBP, is the shared type-safety infrastructure across both the AST (micro) and ConceptDAG (macro) evolution tiers. The existing dictionary, selection engine, and fitness evaluator are reused without modification.
