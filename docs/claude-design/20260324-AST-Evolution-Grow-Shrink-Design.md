# AST Evolution: Grow/Shrink Mutations and Domain-Restricted Word Pools

**Date:** 2026-03-24
**References:** `20260324-Evolution-Experiments-Report.md`, `20260319-AST-Level-Evolution-Design.md`
**Status:** Design

---

## Problem Statement

The AST evolution engine cannot perform program synthesis. Three experiments (symbolic regression, function synthesis, sorting network) demonstrated that the engine's four mutation operators (substitute, perturb, move, control flow wrap/unwrap) cannot change program length. A 2-instruction seed produces only 2-instruction children, regardless of how many generations are run.

Additionally, the substitute mutation draws replacement words from the entire 277-word dictionary. When evolving a numeric function, >95% of substitutions produce type errors (`mat-inv`, `lstat`, `ssplit` substituted into arithmetic code).

This document designs three additions:
1. **Semantic tag coverage** — tag core primitives so tiered substitution actually works
2. **Grow/shrink mutations** — insert and remove AST nodes to explore program length
3. **Domain-restricted word pools** — constrain substitution and growth to relevant words

---

## Current Architecture

### Mutation Pipeline

```
parent bytecode → decompile → AST → mutate → type_repair → compile → child bytecode
```

### Four Existing Operators

| Operator | What it does | Program length |
|---|---|---|
| `substitute_call` | Replace a WordCall with a type-compatible alternative | Unchanged |
| `perturb_constant` | Add Gaussian noise to a numeric Literal | Unchanged |
| `move_block` | Relocate a WordCall within a Sequence | Unchanged |
| `mutate_control_flow` | Wrap WordCall in `if/then` or unwrap existing | +2 or -2 (condition + block markers) |

### Substitution Candidate Selection

`SignatureIndex::find_tiered()` returns candidates matching a `(consumed, produced)` stack effect, ranked by semantic tag overlap:

- **Level 1** (weight 6.0): All semantic tags match — same domain
- **Level 2** (weight 2.5): Partial tag overlap — related domain
- **Level 3** (weight 1.5): Stack effect only — any domain

Current tag coverage is sparse: only 22 matrix/MLP words have semantic tags. Core arithmetic, stack manipulation, and comparison words are untagged.

---

## Prerequisite: Semantic Tag Coverage on Core Primitives

The tiered substitution system (`find_tiered()`) already implements domain-aware word selection — Level 1 prefers exact semantic tag matches, Level 2 allows partial overlap, Level 3 falls back to signature-only. The machinery works. But it's starved of data.

**Current state:** Only 22 of 277 words have semantic tags (the matrix/MLP words tagged during Stage 5 of AST evolution). All core primitives — arithmetic, stack manipulation, comparison, logic, math, string, array — are untagged. This means every substitution falls through to Level 3, where any word with a matching stack effect is equally likely. That's why `mat-inv` and `lstat` get substituted into arithmetic code.

**The fix:** Add `semantic-tags` metadata to all core primitives in `data/help.til`, using the existing category groupings as the tag vocabulary. This requires no C++ changes — the `SignatureIndex` already reads semantic tags from concept metadata on rebuild.

### Proposed Tag Groups

| Tag | Words | Stack Effect Pattern |
|---|---|---|
| `arithmetic` | `+`, `-`, `*`, `/`, `mod`, `/mod`, `negate`, `abs`, `max`, `min` | Mostly `(2,1)` or `(1,1)` |
| `stack` | `dup`, `drop`, `swap`, `over`, `rot`, `nip`, `tuck`, `pick`, `roll`, `?dup`, `depth` | Variable |
| `comparison` | `=`, `<>`, `<`, `>`, `<=`, `>=`, `0=`, `0<`, `0>` | `(2,1)` or `(1,1)` |
| `logic` | `and`, `or`, `xor`, `not`, `invert`, `lshift`, `rshift` | `(2,1)` or `(1,1)` |
| `math` | `sqrt`, `sin`, `cos`, `tan`, `tanh`, `exp`, `log`, `pow`, `ceil`, `floor`, `round`, `pi` | Mostly `(1,1)` |
| `string` | `s+`, `s=`, `slength`, `substr`, `strim`, `sfind`, `sreplace`, `ssplit`, `sjoin` | Variable |
| `array` | `array-new`, `array-push`, `array-pop`, `array-get`, `array-set`, `array-length` | Variable |
| `io` | `.`, `cr`, `emit`, `space`, `type`, `s.` | Mostly `(1,0)` |
| `conversion` | `int->float`, `float->int`, `number->string`, `string->number`, `bool` | `(1,1)` |
| `matrix` | Already tagged (22 words) | Variable |

### Format in help.til

The existing convention uses concept metadata:

```til
s" +" s" semantic-tags" s" text" s" arithmetic" meta! drop
s" -" s" semantic-tags" s" text" s" arithmetic" meta! drop
s" dup" s" semantic-tags" s" text" s" stack" meta! drop
s" swap" s" semantic-tags" s" text" s" stack" meta! drop
s" sin" s" semantic-tags" s" text" s" math" meta! drop
```

Words that span domains get multiple space-separated tags:

```til
s" abs" s" semantic-tags" s" text" s" arithmetic math" meta! drop
s" negate" s" semantic-tags" s" text" s" arithmetic math" meta! drop
```

### Impact

With tags on ~100 core words, the substitute mutation's Level 1 and Level 2 tiers will activate. For a numeric function containing `+`, a substitute mutation will prefer other `arithmetic`-tagged words (`-`, `*`, `/`, `negate`) over `mat-inv` or `ssplit`. This alone should dramatically reduce the >95% mutation rejection rate observed in the experiments.

**This should be implemented before grow/shrink.** The existing substitute mutation will become useful immediately, without any operator changes.

---

## Design: Dynamic Tag and Bridge Registration

The semantic tags and bridge maps defined in `help.til` and `evolution.til` cover built-in words. But user-defined words (via `:`) also need to participate in evolution. A word like `: square dup * ;` is arithmetic; `: array-sum 0 swap ' + array-reduce ;` bridges array→integer. Without registration, user words are invisible to the pool system.

### Two Mechanisms: Manual and Inferred

Both mechanisms coexist. Manual tags take precedence over inferred tags.

#### Manual Registration

Three new TIL words for explicit tagging:

```til
: square dup * ;
s" square" s" arithmetic" evolve-tag            # add to sibling group

: array-sum 0 swap ' + array-reduce ;
s" array-sum" s" array" s" integer" evolve-bridge   # register as array→integer bridge
```

```
evolve-tag     ( word-str tags-str -- )         # set semantic tags (space-separated)
evolve-bridge  ( word-str from-type to-type -- ) # register as type bridge
evolve-untag   ( word-str -- )                  # remove manual tags
```

Manual tags are stored as concept metadata:
- `semantic-tags` — the existing key, same as built-in words in `help.til`
- `bridge-from` / `bridge-to` — new keys for bridge registration

Manual tags are authoritative — if a word has manual tags, inferred tags are ignored for that word.

#### Automatic Inference

When `;` finalizes a colon definition, the compiler already computes a `TypeSignature` (input/output types). Two inferences can be derived automatically:

**Tag inference:** If every `WordCall` in the compiled body has a semantic tag, and all tags share a common value, the new word inherits that tag. Example:

```til
: square dup * ;
#   dup  → tags: [stack]
#   *    → tags: [arithmetic]
#   No common tag → inferred tag: "arithmetic stack" (union of all tags)
```

More conservatively: if >75% of WordCalls share a tag, infer that tag. This avoids tagging a word `arithmetic` just because it has one `+` among 10 string operations.

**Bridge inference:** If the `TypeSignature` has a single input type and a single output type, and they differ, the word is a bridge candidate:

```til
: array-sum 0 swap ' + array-reduce ;
#   TypeSignature: (Array -- Integer)
#   Inferred bridge: Array → Integer
```

Words with `Unknown` or `variable_inputs`/`variable_outputs` signatures are not inferred as bridges.

#### Storage

Inferred tags are stored in separate metadata keys so they don't collide with manual tags:

- `semantic-tags-inferred` — auto-computed at `;` time
- `bridge-from-inferred` / `bridge-to-inferred` — auto-computed from TypeSignature

#### Lookup Priority

When the evolution engine queries a word's tags:

1. Check `semantic-tags` (manual) — if present, use it exclusively
2. Else check `semantic-tags-inferred` — use the auto-computed tags

Same for bridge maps:

1. Check `bridge-from`/`bridge-to` (manual) — if present, use it
2. Else check `bridge-from-inferred`/`bridge-to-inferred`

#### SignatureIndex Rebuild

The `SignatureIndex` already calls `rebuild()` when the dictionary generation changes (new words added). The rebuild reads semantic tags from concept metadata. No change needed — inferred tags stored in metadata are picked up automatically on the next rebuild.

#### Edge Cases

- **Redefining a word** — new `;` recomputes inferred tags for the new implementation. Manual tags on the concept are unaffected (concept-level metadata persists across implementations).
- **`forget`** — removes the implementation but concept metadata (including manual tags) survives if other implementations exist. If the last implementation is removed, the concept and all metadata are erased.
- **No body** — primitives have no bytecode body to analyze. Inference is skipped. Primitives rely on manual tags in `help.til`.
- **Mixed bodies** — a word calling both `arithmetic` and `string` words gets the union tag `"arithmetic string"`. The tiered matching treats this as partial overlap with both pools.

---

## Design: Grow Mutation

### Concept

Insert a new AST node at a random position in a `Sequence`. The type repair pass handles any stack imbalance introduced by the insertion.

### Two Growth Variants

**Grow-Word**: Insert a `WordCall` node from the substitution pool.

```
Before:  [dup, +]
After:   [dup, *, +]     ← inserted * at position 1
```

The stack simulator will detect that `*` consumes 2 values but only 1 is available after `dup`. Type repair inserts a `dup` or `over` to fix:
```
Repaired: [dup, dup, *, +]
```

**Grow-Literal**: Insert an integer or float `Literal` node.

```
Before:  [dup, +]
After:   [dup, 3, +]     ← inserted literal 3 at position 1
```

This pushes an extra value. Type repair may need to insert `swap` or `drop` to balance, or the extra value might be exactly what a subsequent word needs.

### Implementation

New function in `ast_genetic_ops.cpp`:

```cpp
bool grow_node(ASTNode& ast) {
    // 1. Find all Sequence nodes in the AST
    // 2. Pick a random Sequence
    // 3. Pick a random insertion position (0 to children.size())
    // 4. Coin flip: grow-word (70%) or grow-literal (30%)
    //    - grow-word: select from word pool (domain-restricted if configured)
    //      using find_restricted() or find_compatible() with (1,1) default effect
    //    - grow-literal: random integer in [-10, 10] or random float in [-1.0, 1.0]
    // 5. Insert at position
    // 6. Return true
}
```

**Word selection for grow-word**: When no domain pool is configured, use words with small stack effects — prefer `(1,1)` words (one in, one out) like `dup`, `negate`, `abs`, `1+`, `1-`. These are least likely to cause irreparable stack imbalance. With a domain pool, select uniformly from the pool.

**Literal range**: Small integers `[-10, 10]` and small floats `[-1.0, 1.0]` as starting points. The perturb mutation can refine them in subsequent generations.

### Constraints

- Maximum program length: `max_ast_nodes` config parameter (default 30). Prevents bloat.
- Minimum program length for grow: none (can grow from 1 node).
- Growth rate: at most 1 node per mutation call (gradual exploration).

---

## Design: Shrink Mutation

### Concept

Remove a random AST node from a `Sequence`. Type repair handles the resulting stack imbalance. If repair fails, the mutation is rejected (returns null).

### Implementation

New function in `ast_genetic_ops.cpp`:

```cpp
bool shrink_node(ASTNode& ast) {
    // 1. Find all Sequence nodes with ≥2 children
    // 2. Pick a random Sequence
    // 3. Pick a random removable child (WordCall or Literal only;
    //    never remove control flow nodes — too destructive)
    // 4. Erase the child
    // 5. Return true (type repair will fix or reject)
}
```

### Constraints

- Minimum program length: 1 node. Cannot shrink below 1.
- Only `WordCall` and `Literal` nodes are removable. Removing control flow nodes (`IfThen`, `DoLoop`, etc.) would destroy program structure.
- Shrink + type repair may produce a program that type-repairs to something equivalent to the original (repair inserts shuffles that compensate for the removal). This is harmless — fitness evaluation will give it the same score and pruning will handle it.

---

## Design: Domain-Restricted Word Pools

### Concept

Allow the caller to specify a set of words that the evolution engine should draw from when performing substitute and grow mutations. All other words are excluded from consideration.

### Three Integration Points

**1. `evolve-register` TIL word** — accept an optional word pool:

Current signature:
```
evolve-register ( word-str tests-array -- flag )
```

New signature (backward compatible):
```
evolve-register       ( word-str tests-array -- flag )           # no pool — full dictionary
evolve-register-pool  ( word-str tests-array pool-array -- flag ) # restricted pool
```

The pool array contains word name strings:
```til
array-new
  s" +" array-push s" -" array-push s" *" array-push
  s" dup" array-push s" swap" array-push s" over" array-push
  s" rot" array-push s" nip" array-push s" tuck" array-push
variable math-pool
math-pool !

s" target-fn" tests @ math-pool @ evolve-register-pool drop
```

**2. `EvolutionConfig` C++ struct** — store the pool:

```cpp
struct EvolutionConfig {
    // ... existing fields ...
    size_t max_ast_nodes = 30;                    // bloat control
    std::vector<std::string> word_pool;           // empty = full dictionary
};
```

Per-word pool stored in the `EvolutionEngine::WordEvolutionState`:

```cpp
struct WordEvolutionState {
    std::vector<TestCase> test_cases;
    size_t generation_count = 0;
    std::vector<std::string> word_pool;           // empty = use config default
};
```

**3. `SignatureIndex` — pool-filtered lookup**:

New method:

```cpp
std::vector<std::string> find_restricted(
    int consumed, int produced,
    const std::vector<std::string>& pool) const;
```

Filters `find_compatible()` results to only include words in the pool. Falls back to full dictionary if pool is empty.

For `substitute_call`, the existing `find_tiered()` would gain an optional pool parameter. When a pool is provided, Level 3 (signature-only) candidates are restricted to the pool. Levels 1 and 2 (tag-matched) remain pool-restricted too.

### Pool Taxonomy: Sibling Pools and Bridge Maps

There are two distinct kinds of word pools serving different evolutionary purposes:

**Sibling pools** constrain mutations within a single domain. The substitute mutation draws replacements from the sibling pool — `+` is replaced by `-` or `*`, never by `mat-inv` or `ssplit`. This keeps mutations semantically coherent within one type domain.

**Bridge maps** enable mutations that cross between domains through the type system. A bridge word converts a value from one ETIL type to another. When the grow mutation needs to connect two sibling domains (e.g., an array pipeline feeding into arithmetic), it consults the bridge map to find a type-correct pathway instead of inserting a random word and hoping type repair saves it.

#### Sibling Pools

Common domain pools defined in TIL for convenience (in `data/library/` or `data/help.til`):

```til
# Arithmetic domain
: math-pool
  array-new
  s" +" array-push s" -" array-push s" *" array-push
  s" /" array-push s" mod" array-push s" negate" array-push
  s" abs" array-push s" max" array-push s" min" array-push
  s" dup" array-push s" swap" array-push s" over" array-push
  s" rot" array-push s" drop" array-push s" nip" array-push
;

# Matrix domain
: matrix-pool
  array-new
  s" mat*" array-push s" mat+" array-push s" mat-" array-push
  s" mat-scale" array-push s" mat-transpose" array-push
  s" mat-hadamard" array-push s" mat-add-col" array-push
  s" mat-relu" array-push s" mat-sigmoid" array-push s" mat-tanh" array-push
;

# String domain
: string-pool
  array-new
  s" s+" array-push s" slength" array-push s" substr" array-push
  s" strim" array-push s" sfind" array-push s" sreplace" array-push
  s" ssplit" array-push s" sjoin" array-push
;
```

#### Bridge Maps

A bridge map is keyed by `(from-type, to-type)` and maps to the words that perform that conversion. This is a different data structure from sibling pools — it's a map of maps, not an array.

| From | To | Bridge Words |
|---|---|---|
| Array | Integer | `array-length`, `array-reduce` |
| Array | String | `sjoin` |
| Array | Matrix | `array->mat` |
| String | Integer | `slength`, `string->number` |
| String | Float | `string->number` |
| String | Array | `ssplit` |
| String | ByteArray | `string->bytes` |
| Integer | Float | `int->float` |
| Integer | String | `number->string` |
| Float | Integer | `float->int` |
| Float | String | `number->string` |
| Matrix | Float | `mat-norm`, `mat-trace`, `mat-det`, `mat-mean`, `mat-sum` |
| Matrix | Integer | `mat-rows`, `mat-cols` |
| Matrix | Array | `mat->array` |
| Matrix | Json | `mat->json` |
| ByteArray | String | `bytes->string` |
| ByteArray | Integer | `bytes-length` |
| Json | String | `json-dump` |
| Json | Array | `json->array` |
| Json | Map | `json->map` |
| Map | Json | `map->json` |
| Map | Array | `map-keys`, `map-values` |
| Map | Integer | `map-length` |
| Any | Boolean | `0=`, `0<`, `0>`, `bool`, `exists?`, `map-has?`, `obs?` |

#### How Bridge Maps Interact with Evolution

**Substitute mutation**: Stays within sibling pools. No bridge involvement.

**Grow mutation**: When inserting a word, the grow operator checks the current stack state (simulated types) and the requirements of adjacent words. If the stack has an Array but the next word expects an Integer, the grow operator consults the bridge map for `(Array → Integer)` and inserts `array-length` — a type-correct domain crossing.

**Crossover**: When combining ASTs from two parents that operate in different domains, bridge words at the crossover point ensure type compatibility. Parent A produces a Matrix; parent B expects a Float. The crossover inserts `mat-norm` at the junction.

#### Data Structure

In C++, the bridge map is a nested map:

```cpp
// key: (input_type, output_type), value: word names
using BridgeMap = std::map<
    std::pair<TypeSignature::Type, TypeSignature::Type>,
    std::vector<std::string>>;
```

In TIL, represented as a nested map:

```til
# Bridge map: type conversions
map-new
  s" array->integer" array-new s" array-length" array-push s" array-reduce" array-push map-set
  s" string->integer" array-new s" slength" array-push s" string->number" array-push map-set
  s" integer->float" array-new s" int->float" array-push map-set
  s" matrix->float" array-new s" mat-norm" array-push s" mat-trace" array-push s" mat-mean" array-push map-set
  # ... etc
variable bridge-map
bridge-map !
```

The bridge map can be built automatically from `TypeSignature` data — any word with a single-type input and a different single-type output is a candidate bridge word. But manual curation is preferable to avoid nonsensical bridges (e.g., `emit` converts Integer → nothing, but it's not a useful bridge — it's a side effect).

#### Relationship Between Pools

```
┌─────────────────────────────────────────┐
│           Sibling Pools                 │
│                                         │
│  ┌─────────┐  ┌─────────┐  ┌────────┐  │
│  │  math   │  │ matrix  │  │ string │  │
│  │ + - * / │  │ mat* m+ │  │ s+ sub │  │
│  │ dup abs │  │ m-scale │  │ sfind  │  │
│  └────┬────┘  └────┬────┘  └───┬────┘  │
│       │            │           │        │
│       └──────┬─────┘           │        │
│              │                 │        │
│     ┌────────┴────────┐       │        │
│     │  Bridge Words   │       │        │
│     │                 │       │        │
│     │  array-length   ├───────┘        │
│     │  int->float     │                │
│     │  mat-norm       │                │
│     │  string->number │                │
│     │  number->string │                │
│     └─────────────────┘                │
└─────────────────────────────────────────┘

Substitute: stays within a sibling pool
Grow:       inserts from sibling pool OR bridge map
Crossover:  bridge words at domain junctions
```

---

## Mutation Dispatch: Updated Operator Set

With grow and shrink added, the dispatch becomes 6 operators:

| # | Operator | Weight | Effect on Length |
|---|---|---|---|
| 0 | `substitute_call` | 30% | 0 |
| 1 | `perturb_constant` | 15% | 0 |
| 2 | `move_block` | 10% | 0 |
| 3 | `mutate_control_flow` | 10% | +2 or -2 |
| 4 | `grow_node` | 20% | +1 |
| 5 | `shrink_node` | 15% | -1 |

The grow/shrink weights (20%/15%) create a slight bias toward growth, which is typical in genetic programming — bloat control via `max_ast_nodes` prevents runaway expansion.

### Configuration

```cpp
struct MutationWeights {
    double substitute = 0.30;
    double perturb    = 0.15;
    double move       = 0.10;
    double control    = 0.10;
    double grow       = 0.20;
    double shrink     = 0.15;
};
```

Exposed in `EvolutionConfig`. Not exposed to TIL (too granular — C++ config only).

---

## Bloat Control

Genetic programming suffers from bloat — programs grow without bound as neutral mutations accumulate. Three defenses:

1. **Hard limit**: `max_ast_nodes` (default 30). Grow mutation is suppressed when the AST exceeds this size. Allows shrink to continue.

2. **Parsimony pressure**: Add a small length penalty to the fitness score:
   ```
   adjusted_fitness = correctness - length_penalty * node_count
   ```
   With `length_penalty = 0.001`, a 10-node program scoring 0.9 correctness gets 0.89, while a 5-node program scoring 0.9 gets 0.895. Shorter programs win ties.

3. **Minimum Description Length**: After convergence (100% correctness), run a shrink-only phase that removes nodes one at a time, keeping the program only if it still passes all tests. This produces the shortest correct program.

---

## Type Repair Interaction

Both grow and shrink mutations will produce ASTs with stack imbalance. The existing `type_repair.cpp` handles this by inserting shuffle words (`swap`, `rot`, `roll`). Two considerations:

**Grow**: Inserting a `(1,1)` word (like `abs` or `negate`) is stack-neutral — no repair needed. Inserting a `(2,1)` word (like `+` or `*`) consumes an extra value — repair may insert `dup` or `over` to provide it. Inserting a `(0,1)` word (like a literal or `depth`) adds a value — repair may insert `drop` to consume it.

**Shrink**: Removing a `(1,1)` word is stack-neutral. Removing a `(0,1)` word (literal) reduces the available stack depth — repair may need to insert a literal or `dup` to compensate. Removing a `(2,1)` word leaves an extra value on the stack — repair may insert `drop`.

Current type repair only inserts shuffles (`swap`, `rot`, `roll`). It cannot insert `dup`, `drop`, or literals. This is a limitation:

**Option A**: Extend type repair to insert `dup`/`drop` when shuffles are insufficient. This is a surgical change to `repair_sequence()` — when `find_type_in_stack()` returns -1 (type not found), try inserting `dup` of the nearest compatible type instead of failing.

**Option B**: Accept that some grow/shrink mutations will be rejected by type repair (return null). The evolution loop already handles null returns by trying the next operator. This is simpler and may be sufficient — the grow mutation's bias toward `(1,1)` words minimizes rejection.

**Recommendation**: Start with Option B. Measure rejection rates. If >50% of grow/shrink mutations are rejected, implement Option A.

---

## Implementation Plan

### Phase 0: Evolution Debug Logging (implement first — used to debug all subsequent phases)

| Step | File | Change |
|---|---|---|
| 0 | `include/etil/evolution/evolve_logger.hpp` | New: `EvolveLogger` class, `EvolveLogCategory` enum |
| 1 | `src/evolution/evolve_logger.cpp` | New: File management, timestamped output, category filtering |
| 2 | `evolution_engine.hpp` | Add `LogLevel`, log config fields to `EvolutionConfig` |
| 3 | `ast_genetic_ops.cpp` | Add `EvolveLogger&` parameter to all 4 operators, insert log calls |
| 4 | `evolution_engine.cpp` | Log generation loop, child creation, weight update, pruning |
| 5 | `type_repair.cpp` | Log mismatch detection, shuffle insertion |
| 6 | `fitness.cpp` | Log per-test-case results at granular level |
| 7 | `primitives.cpp` | Add `evolve-log-start`, `evolve-log-stop`, `evolve-log-dir` words |
| 8 | Tests | Verify logging output at both levels, verify zero overhead when off |

After Phase 0: Re-run the symbolic regression experiment with logging enabled to see what the pipeline is actually doing.

### Phase 1: Semantic Tag Coverage (TIL only, no C++ changes)

| Step | File | Change |
|---|---|---|
| 9 | `data/help.til` | Add `semantic-tags` metadata to ~100 core primitives |

Without tags, the tiered substitution falls through to Level 3 (any word), making substitute mutation useless for domain-specific evolution. After tagging, re-run the symbolic regression experiment with logging to measure the improvement before adding grow/shrink.

### Phase 2: Grow/Shrink Operators (C++ only)

| Step | File | Change |
|---|---|---|
| 10 | `evolution_engine.hpp` | Add `max_ast_nodes`, `MutationWeights` to `EvolutionConfig` |
| 11 | `ast_genetic_ops.hpp` | Declare `grow_node()`, `shrink_node()` |
| 12 | `ast_genetic_ops.cpp` | Implement `grow_node()`, `shrink_node()`, update `mutate()` dispatch to 6 operators |
| 13 | `ast.hpp` | Add `count_nodes()` helper for bloat control |
| 14 | `evolve_logger.cpp` | Add log calls to grow/shrink operators |
| 15 | Tests | New unit tests for grow/shrink in `test_ast_genetic_ops.cpp` |

### Phase 3: Sibling Pools, Bridge Maps, and Domain-Restricted Evolution

| Step | File | Change |
|---|---|---|
| 16 | `signature_index.hpp/cpp` | Add `find_restricted()` and `find_bridge()` methods |
| 17 | `ast_genetic_ops.cpp` | Pass sibling pool to substitute; consult bridge map in grow for domain crossings |
| 18 | `evolution_engine.hpp/cpp` | Store per-word sibling pool and bridge map in `WordEvolutionState` |
| 19 | `primitives.cpp` | Add `evolve-register-pool` word accepting sibling pool and optional bridge map |
| 20 | `data/help.til` or `data/library/evolution.til` | Define predefined sibling pools and bridge map |
| 21 | Tests | Test restricted substitution, grow with pool, bridge insertion at type boundaries |

### Phase 4: Validation

| Step | Task |
|---|---|
| 22 | Re-run symbolic regression with grow/shrink + math pool + logging |
| 23 | Re-run function synthesis experiment |
| 24 | Measure mutation rejection rates (logged) |
| 25 | Tune weights and `max_ast_nodes` based on log analysis |
| 26 | Update experiments report |

### Estimated Effort

| Phase | Solo Human | AI-Assisted |
|---|---|---|
| Phase 0: Evolution debug logging | 1 day | 2 hours |
| Phase 1: Semantic tags | 3 hours | 30 min |
| Phase 2: Grow/shrink operators | 2 days | 3-4 hours |
| Phase 3: Sibling pools + bridge maps | 1.5 days | 2-3 hours |
| Phase 4: Validation | 1 day | 1 hour |
| **Total** | **~6 days** | **~9 hours** |

---

## Design: Evolution Debug Logging

The evolution pipeline is opaque — when a mutation fails or produces unexpected results, there's no way to see what happened. The pipeline needs structured logging at two levels with category-based filtering. **This is the first feature to implement** — it will be used to debug all subsequent work (semantic tags, grow/shrink, pools).

Evolution logging is completely separate from spdlog, interpreter output, and MCP response streams. It writes to its own dedicated log files.

### Log File Convention

Log files are written to a configurable directory (default: current working directory). File names are timestamped for canonical sorting:

```
YYYYMMDDThhmmss-evolve.log
```

Examples:
```
20260324T143052-evolve.log
20260324T153217-evolve.log
```

A new log file is created each time logging is enabled (via `evolve-log-start`). The timestamp is the moment logging starts, not the moment evolution runs. Multiple evolution runs in one session append to the same log file.

### Two Logging Levels

**Logical level** — A running narrative of what the evolution engine is doing, written for a human reading a log file. No C++ function names, no memory addresses, no implementation details. Answers: "What happened and why?"

Each line is timestamped:

```
2026-03-24T14:30:52.001 [evolve] Gen 12: mutating 'target-fn' impl#47 (generation 3, weight 0.72)
2026-03-24T14:30:52.001 [mutate] Selected operator: substitute_call
2026-03-24T14:30:52.002 [substitute] Word 'dup' at AST position 3 → candidates: [+, -, *, negate, abs] (Level 1: arithmetic)
2026-03-24T14:30:52.002 [substitute] Replaced 'dup' with '*'
2026-03-24T14:30:52.003 [repair] Type mismatch at position 4: '*' expects (int, int) but stack has (int)
2026-03-24T14:30:52.003 [repair] Inserted 'dup' before position 4 to duplicate TOS
2026-03-24T14:30:52.004 [compile] Compiled 5-node AST → 7 instructions
2026-03-24T14:30:52.005 [fitness] Child impl#52: 3/10 tests pass (0.30 correctness), 450ns mean
2026-03-24T14:30:52.010 [evolve] Gen 12: 5 children created, best weight 0.72 → 0.35 (normalized)
2026-03-24T14:30:52.010 [prune] Removed impl#38 (weight 0.01, below threshold 0.01)
```

**Granular level** — Function-level tracing with parameters and return values. Written for debugging the evolution engine C++ code itself. Includes everything from logical level plus detail lines:

```
2026-03-24T14:30:52.002 [substitute] Word 'dup' at AST position 3 → candidates: [+, -, *, negate, abs] (Level 1: arithmetic)
2026-03-24T14:30:52.002 [substitute:detail] find_tiered(consumed=2, produced=1) → 47 candidates
2026-03-24T14:30:52.002 [substitute:detail] Level 1 (arithmetic): [+, -, *, /, mod] (5 words, weight 6.0)
2026-03-24T14:30:52.002 [substitute:detail] Level 2 (math): [pow, fmin, fmax] (3 words, weight 2.5)
2026-03-24T14:30:52.002 [substitute:detail] Level 3 (signature-only): [mat+, s+, ...] (39 words, weight 1.5)
2026-03-24T14:30:52.002 [substitute:detail] Weighted random selected '*' from Level 1
2026-03-24T14:30:52.003 [repair:detail] simulate_node(WordCall '+', stack=[Unknown, Unknown]) → consumed=2, produced=1
2026-03-24T14:30:52.003 [repair:detail] find_type_in_stack(needed=Unknown, start=0) → found at 0
2026-03-24T14:30:52.004 [compile:detail] emit Instruction{Op::Call, word='dup'} at ip=3
2026-03-24T14:30:52.005 [fitness:detail] Test 0: input=[0], expected=[5], got=[0], pass=false (42ns)
2026-03-24T14:30:52.005 [fitness:detail] Test 1: input=[1], expected=[9], got=[1], pass=false (38ns)
```

### Category Mask

A bitmask enables/disables logging for specific categories independently. Each category applies to both logical and granular levels — the level controls verbosity, the mask controls which subsystems emit.

```cpp
enum class EvolveLogCategory : uint32_t {
    None        = 0,
    Engine      = 1 << 0,   // Generation loop, child creation, population management
    Decompile   = 1 << 1,   // Bytecode → AST conversion
    Substitute  = 1 << 2,   // Word substitution mutation
    Perturb     = 1 << 3,   // Constant perturbation mutation
    Move        = 1 << 4,   // Block move mutation
    ControlFlow = 1 << 5,   // Control flow wrap/unwrap mutation
    Grow        = 1 << 6,   // Grow mutation (future)
    Shrink      = 1 << 7,   // Shrink mutation (future)
    Crossover   = 1 << 8,   // AST crossover between parents
    Repair      = 1 << 9,   // Type repair (shuffle insertion)
    Compile     = 1 << 10,  // AST → bytecode compilation
    Fitness     = 1 << 11,  // Test case evaluation
    Selection   = 1 << 12,  // Parent selection, weight update, pruning
    Bridge      = 1 << 13,  // Bridge word insertion (future)
    All         = 0xFFFFFFFF,
};
```

### TIL Interface

Three words control evolution logging:

```
evolve-log-start  ( level mask -- )   # Open log file, start logging
evolve-log-stop   ( -- )              # Flush and close log file
evolve-log-dir    ( path -- )         # Set log directory (default: cwd)
```

- `level`: 0 = off (close file), 1 = logical, 2 = granular
- `mask`: bitmask of `EvolveLogCategory` values (0xFFFFFFFF = all)

Usage:
```til
s" /tmp" evolve-log-dir                     # logs go to /tmp/

1 0xFFFF evolve-log-start                   # logical, all categories
s" target-fn" evolve-word drop              # evolution runs are logged
evolve-log-stop                             # flush and close

2 0x0204 evolve-log-start                   # granular, substitute + repair only
s" target-fn" evolve-word drop
evolve-log-stop

# Log file created: /tmp/20260324T143052-evolve.log
```

### C++ Configuration

```cpp
struct EvolutionConfig {
    // ... existing fields ...

    // Logging — controlled via TIL words, not set directly
    enum class LogLevel { Off, Logical, Granular };
    LogLevel log_level = LogLevel::Off;
    uint32_t log_categories = static_cast<uint32_t>(EvolveLogCategory::All);
    std::string log_directory;  // empty = cwd
};
```

### Implementation

**New files** (evolution logging is self-contained, not mixed with other subsystems):

- `include/etil/evolution/evolve_logger.hpp` — `EvolveLogger` class declaration, `EvolveLogCategory` enum
- `src/evolution/evolve_logger.cpp` — File management, timestamp formatting, log emission

```cpp
class EvolveLogger {
public:
    explicit EvolveLogger(EvolutionConfig& config);
    ~EvolveLogger();  // closes file if open

    // Lifecycle
    void start(EvolutionConfig::LogLevel level, uint32_t mask);
    void stop();
    void set_directory(const std::string& dir);

    // Check before formatting (avoid string construction when disabled)
    bool enabled(EvolveLogCategory cat) const;
    bool granular(EvolveLogCategory cat) const;

    // Emit a log line (timestamp + category tag auto-prepended)
    void log(EvolveLogCategory cat, const std::string& msg);
    void detail(EvolveLogCategory cat, const std::string& msg);

private:
    EvolutionConfig& config_;
    std::ofstream file_;           // dedicated log file
    std::string directory_;

    std::string timestamp() const; // "2026-03-24T14:30:52.001"
    std::string make_filename() const; // "20260324T143052-evolve.log"
};
```

Each evolution subsystem receives the logger and checks `enabled()` before formatting:

```cpp
bool substitute_call(ASTNode& ast, EvolveLogger& log) {
    // ... select node ...
    if (log.enabled(EvolveLogCategory::Substitute)) {
        log.log(EvolveLogCategory::Substitute,
                "Word '" + old_name + "' at position " + std::to_string(pos)
                + " → " + std::to_string(candidates.size()) + " candidates");
    }
    // ... perform substitution ...
    if (log.granular(EvolveLogCategory::Substitute)) {
        log.detail(EvolveLogCategory::Substitute,
                   "Level 1: [" + join(level1) + "] (" + std::to_string(level1.size()) + " words)");
    }
}
```

### Cost When Disabled

When `log_level == Off`, `enabled()` and `granular()` are inline single-comparison returns (`false`). No string formatting, no file I/O, no timestamp computation. Zero overhead in production. The log file is not opened until `evolve-log-start` is called.

---

## Risk Assessment

**Low risk**: Grow/shrink are additive — they don't change existing operator behavior. All existing tests remain valid. The mutation pipeline already handles null returns from operators.

**Medium risk**: Bloat. Without parsimony pressure, programs may grow to `max_ast_nodes` without improving fitness. Mitigation: start with `max_ast_nodes = 30` and `length_penalty = 0.001`.

**Low risk**: Type repair rejection. Some grow/shrink mutations will produce unrepairable ASTs. The pipeline handles this gracefully (try next operator, or skip child). Mitigation: bias grow toward `(1,1)` stack-neutral words.

**No risk to native build**: All changes are in the evolution subsystem, which has no impact on interpreter correctness or WASM build.
