# Type-Directed Bridges — Implementation Plan

**Date:** 2026-04-03
**References:** `20260403A-Evolution-Priority-Review.md`, `20260325-Type-Directed-Bridges-Design.md`
**Prerequisites:** v1.11.5 (distance fitness implemented, bridge map defined in TIL)
**Status:** Plan
**Workflow:** See `docs/claude-knowledge/20260403A-feature-branch-workflow.md`

---

## Overview

Implement the Type-Directed Bridges system as defined in the Bridges design doc. The goal is to eliminate the 90% mutation crash floor by making `substitute_call()` and `grow_node()` type-aware, and enhancing type repair to insert bridge words.

**Starting version:** v1.11.5 (master)
**Feature branch:** Created via `scripts/branch.sh` → v1.11.6
**Each phase:** Implement, unit test, commit, bump patch version
**Final phase:** Update docs (README.md, help.til), merge to master, tag

---

## Phase 0 — Bridge Map C++ Infrastructure

**Goal:** Load the TIL-defined bridge map into a C++ data structure accessible by the evolution engine.

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/bridge_map.hpp` | New: `BridgeMap` class |
| `src/evolution/bridge_map.cpp` | New: implementation |
| `tests/unit/evolution/bridge_map_test.cpp` | New: unit tests |
| `CMakeLists.txt` | Add new source files |

**Design:**

```cpp
namespace etil::evolution {

/// Type conversion edge in the bridge graph.
struct BridgeEdge {
    TypeSignature::Type from;
    TypeSignature::Type to;
    std::string word;  // the conversion word (e.g., "int->float")
};

/// Directed graph of type conversions loaded from TIL bridge map.
class BridgeMap {
public:
    /// Register a single conversion.
    void add(TypeSignature::Type from, TypeSignature::Type to, const std::string& word);

    /// Direct conversions from a given type.
    const std::vector<BridgeEdge>& conversions_from(TypeSignature::Type from) const;

    /// Find a single-hop bridge word: from → to. Returns empty if none.
    std::vector<std::string> find_bridge(TypeSignature::Type from, TypeSignature::Type to) const;

    /// Find a multi-hop path (BFS, max 2 hops). Returns ordered word sequence.
    std::vector<std::string> find_path(TypeSignature::Type from, TypeSignature::Type to, size_t max_hops = 2) const;

    /// Is there any conversion from this type?
    bool has_conversions(TypeSignature::Type from) const;

    size_t size() const;
};

} // namespace etil::evolution
```

**Unit tests:**
- Load 22 conversions, verify `size() == 22`
- `find_bridge(Integer, Float)` returns `{"int->float"}`
- `find_bridge(Integer, Integer)` returns empty (no self-loop)
- `find_bridge(Array, Float)` returns empty (no direct edge)
- `find_path(Array, Float, 2)` returns `{"array-length", "int->float"}` (2-hop)
- `find_path(Integer, Integer, 2)` returns empty (no useful path)
- `conversions_from(Matrix)` returns 4 edges (Float, Integer, Array, Json)
- `has_conversions(Unknown)` returns false

**Commit:** `v1.11.7 — Add BridgeMap C++ infrastructure`

---

## Phase 1 — SignatureIndex Type Filtering

**Goal:** Add `find_type_compatible()` to `SignatureIndex` — filter candidates by input types, not just stack depth.

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/signature_index.hpp` | Add `find_type_compatible()` method |
| `src/evolution/signature_index.cpp` | Implement type-compatible filtering |
| `tests/unit/evolution/signature_index_test.cpp` | New tests for type filtering |

**Design:**

```cpp
/// Find words matching (consumed, produced) whose input types are compatible
/// with the given stack types. Falls back to find_compatible() if stack_types
/// is empty or all Unknown.
std::vector<std::string> find_type_compatible(
    int consumed, int produced,
    const std::vector<TypeSignature::Type>& stack_types) const;
```

Compatibility rules:
- `Unknown` in the word's signature matches any stack type (permissive)
- `Unknown` on the stack matches any word input type (permissive)
- Concrete types must match exactly (Integer needs Integer, not String)
- If no type-compatible candidates exist, fall back to depth-only (never return empty)

**Unit tests:**
- Register `+` (Integer, Integer → Integer) and `copy-file` (String, String → Integer)
- `find_type_compatible(2, 1, {Integer, Integer})` includes `+`, excludes `copy-file`
- `find_type_compatible(2, 1, {String, String})` includes `copy-file`, excludes `+`
- `find_type_compatible(2, 1, {Unknown, Unknown})` includes both (Unknown is permissive)
- `find_type_compatible(2, 1, {})` falls back to `find_compatible()` (all candidates)
- Verify fallback: if no type-compatible match, returns depth-only results

**Commit:** `v1.11.8 — Add find_type_compatible() to SignatureIndex`

---

## Phase 2 — Stack Simulator Input Type Tracking

**Goal:** Enhance the stack simulator to report the stack type state at each AST node position, so mutation operators can query "what types are on the stack at this point?"

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/stack_simulator.hpp` | Add `types_at()` method or per-node type annotation |
| `src/evolution/stack_simulator.cpp` | Store type stack snapshot per node during `annotate()` |
| `tests/unit/evolution/stack_simulator_test.cpp` | New tests for input type tracking |

**Design:**

The stack simulator already maintains a `SimState::type_stack` during `annotate()`. Currently it records `effect.consumed` and `effect.produced` counts per node but discards the type stack contents. The change stores a snapshot of the top N types at each node position.

```cpp
/// Type state at a specific AST node position.
struct TypeState {
    std::vector<TypeSignature::Type> stack_types;  // types from TOS downward
    bool valid = true;  // false if simulation state was invalid at this point
};

/// Get the type state at a specific node index (after annotate()).
TypeState types_at(size_t node_index) const;
```

**Unit tests:**
- AST for `dup +` with Integer input: types_at(0) = `{Integer}`, types_at(1) = `{Integer, Integer}`
- AST for `3 5 +`: types_at(0) = `{}`, types_at(1) = `{Integer}`, types_at(2) = `{Integer, Integer}`
- AST with `Unknown` input signature: types contain `Unknown`
- AST with opaque word (`execute`): types_at after opaque returns `valid = false`

**Commit:** `v1.11.9 — Stack simulator tracks input types at each AST node`

---

## Phase 3 — Type-Directed Substitute

**Goal:** Modify `substitute_call()` to use `find_type_compatible()` instead of depth-only matching.

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/ast_genetic_ops.hpp` | Add `BridgeMap*` to constructor or config |
| `src/evolution/ast_genetic_ops.cpp` | `substitute_call()` queries stack types, uses type filtering |
| `tests/unit/evolution/ast_genetic_ops_test.cpp` | New tests for type-directed substitution |

**Design:**

Current flow (`ast_genetic_ops.cpp:38-114`):
```
Pick word → find_restricted() or find_tiered() by depth → random selection
```

New flow:
```
Pick word → simulate types at position → find_type_compatible() → tiered ranking → selection
```

The tiered ranking (semantic tags) is preserved as a secondary sort within the type-compatible set.

**Unit tests:**
- Substitute in `dup +` with Integer stack: `+` replaced by `*`, `-`, `mod` (all Integer, Integer → Integer), never by `copy-file`, `s+`
- Substitute with pool restriction: type filtering applies within the pool
- Substitute with Unknown types: falls back to depth-only (existing behavior preserved)
- Substitute where no type-compatible match exists: falls back gracefully

**Commit:** `v1.11.10 — Type-directed substitute_call()`

---

## Phase 4 — Type-Directed Grow

**Goal:** Modify `grow_node()` to select type-legal words based on the stack type at the insertion point. Bridge words appear naturally as candidates.

**Files:**
| File | Change |
|---|---|
| `src/evolution/ast_genetic_ops.cpp` | `grow_node()` queries stack types, filters (1,1) words by input type |
| `tests/unit/evolution/ast_genetic_ops_test.cpp` | New tests for type-directed grow |

**Design:**

Current flow (`ast_genetic_ops.cpp:301-382`):
```
Check bloat limit → pick random (1,1) word (70%) or literal (30%) → insert
```

New flow:
```
Check bloat limit → simulate types at insertion point →
  70%: find_type_compatible(1, 1, stack_types) → includes bridges naturally
  30%: literal (unchanged)
→ insert
```

When the stack has `[Integer]`, candidates include: `abs`, `negate`, `1+`, `1-`, `dup` (Integer → Integer), `int->float` (Integer → Float, a bridge), `number->string` (Integer → String, a bridge). The bridge words appear as type-legal options without special-casing.

**Unit tests:**
- Grow into `dup +` with Integer stack: inserted word accepts Integer input
- Grow candidates include bridge words (`int->float`) when stack is Integer
- Grow candidates exclude `slength` (needs String) when stack is Integer
- Grow with Unknown stack: falls back to any (1,1) word
- Bloat control still enforced (max_ast_nodes)

**Commit:** `v1.11.11 — Type-directed grow_node()`

---

## Phase 5 — Cycle Detection

**Goal:** Prevent no-op bridge loops like `int->float float->int` that waste AST node budget.

**Files:**
| File | Change |
|---|---|
| `src/evolution/ast_genetic_ops.cpp` | Adjacent-inverse check before bridge insertion |
| `tests/unit/evolution/ast_genetic_ops_test.cpp` | Cycle detection tests |

**Design:** Option B from the Bridges design — AST-level adjacent-inverse detection.

Before inserting a bridge word at position N, scan position N-1 and N+1 for the inverse bridge. If `int->float` is adjacent, don't insert `float->int`. The `max_ast_nodes` bloat control (default 30) is the safety net for non-adjacent loops — no-op conversions waste node budget, causing parsimony pressure to remove them.

**Inverse bridge pairs** (derived from the bridge map):
- `int->float` / `float->int`
- `string->bytes` / `bytes->string`
- `ssplit` / `sjoin` (String ↔ Array)
- `map->json` / `json->map`
- `array->mat` / `mat->array`

```cpp
/// Check if inserting bridge_word at position is an inverse of an adjacent bridge.
bool is_inverse_bridge(const ASTNode& ast, size_t position, const std::string& bridge_word) const;
```

**Unit tests:**
- `int->float` at position N, inserting `float->int` at N+1: rejected
- `int->float` at position N, inserting `abs` at N+1: allowed
- `int->float` at position N, inserting `float->int` at N+3: allowed (not adjacent)
- Non-bridge words: never rejected by cycle detection

**Commit:** `v1.11.12 — Adjacent-inverse bridge cycle detection`

---

## Phase 6 — Bridge Logging

**Goal:** Wire bridge insertions into the existing Bridge logging category so they appear in evolution logs.

**Files:**
| File | Change |
|---|---|
| `src/evolution/ast_genetic_ops.cpp` | Log bridge insertions at `EvolveLogCategory::Bridge` |
| `src/evolution/evolve_logger.cpp` | Format bridge log entries |
| `tests/unit/evolution/evolve_logger_test.cpp` | Verify bridge log output |

**Design:**

The `EvolveLogCategory::Bridge` category already exists in `evolve_logger.hpp:37` but is marked "future." Enable it to log:
- Bridge word inserted by `grow_node()`: `[Bridge] grow: int->float (Integer → Float) at position 3`
- Bridge candidate offered by `substitute_call()`: `[Bridge] substitute: 5 type-compatible candidates (3 bridges filtered)`
- Cycle detection rejection: `[Bridge] cycle: float->int rejected (inverse of int->float at position 2)`

**Unit tests:**
- Bridge insertion produces log entry with correct category
- Cycle rejection produces log entry
- Bridge logging disabled when `EvolveLogCategory::Bridge` is off

**Commit:** `v1.11.13 — Bridge logging integration`

---

## Phase 7 — Type Repair Enhancement

**Goal:** Enhance `type_repair` to insert bridge words from the bridge map when a type mismatch is found, not just shuffle existing values.

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/type_repair.hpp` | Add `BridgeMap*` to constructor |
| `src/evolution/type_repair.cpp` | Insert bridge words for type mismatches |
| `tests/unit/evolution/type_repair_test.cpp` | Bridge insertion tests |

**Design:**

Current repair logic (`type_repair.cpp:89-132`):
```
For each WordCall:
  Check input types against simulated stack
  If mismatch: search deeper in stack for needed type → insert shuffle
  If not found on stack at all: return false (unrepairable)
```

Enhanced logic:
```
For each WordCall:
  Check input types against simulated stack
  If mismatch:
    1. Search deeper in stack → insert shuffle (existing)
    2. If not found: consult bridge map for conversion from stack TOS type to needed type
    3. If bridge exists: insert bridge word before the WordCall
    4. If no bridge: return false (unrepairable)
```

Example: Stack has `[Array]`, word needs `Integer`. Bridge map has `Array → Integer` via `array-length`. Insert `array-length` before the word.

**Unit tests:**
- Stack `[Array]`, word needs Integer: `array-length` inserted, R column populated
- Stack `[Integer]`, word needs Float: `int->float` inserted
- Stack `[Integer]`, word needs Integer: no repair needed (types match)
- Stack `[Observable]`, word needs Integer: no bridge exists → repair returns false
- Multi-hop: Stack `[Array]`, word needs Float: `array-length` + `int->float` (2 bridges)
- Verify R column in diff logging populates with bridge word name

**Commit:** `v1.11.14 — Type repair inserts bridge words`

---

## Phase 8 — Integration Validation

**Goal:** Run the symbolic regression benchmark with full dictionary (no pool restriction) and validate that the crash floor is eliminated.

**Files:**
| File | Change |
|---|---|
| `tests/til/evolution/type_directed_validation.til` | New: integration test |
| `tests/til/evolution/type_directed_validation.sh` | New: expected output / pass criteria |

**Design:**

Run 100 generations of `f(x) = x^2 + 3x + 5` evolution with:
- Full dictionary (no pool restriction)
- Distance fitness mode
- Type-directed mutations enabled
- Bridge logging enabled

**Success criteria:**
- Mutation crash rate < 10% (was ~90%)
- Children with fitness > 0.1: > 90% (was ~5-10%)
- Fitness shows upward trend past Gen 6 (was flatline at 0.107)
- Bridge log entries present (bridges are being used)
- R column populates in at least some mutations

**Commit:** `v1.11.15 — Integration validation for type-directed bridges`

---

## Phase 9 — Documentation and Merge

**Goal:** Update user-facing documentation, merge feature branch to master, tag, push.

**Steps:**

1. Update `README.md`:
   - Add type-directed bridges to evolution engine feature list
   - Document `BridgeMap` and type-directed mutation behavior
   - Update word count / feature summary

2. Update `data/help.til`:
   - Add help entries for any new TIL-facing words or configuration

3. Update `CLAUDE.md`:
   - Reflect new evolution engine capabilities

4. Commit documentation updates on feature branch

5. Run `scripts/super-push.sh --message "Type-directed bridges: eliminate mutation crash floor"`
   - Builds all, tests all
   - Merges feature branch to master
   - Resolves version conflicts
   - Tags final version

6. Follow the push workflow in `docs/claude-knowledge/20260403A-feature-branch-workflow.md` (CI first, then GitHub, permission required for each)

---

## Version Summary

| Phase | Version | Description |
|---|---|---|
| — | v1.11.5 | Master before feature branch |
| branch | v1.11.6 | Feature branch created |
| 0 | v1.11.7 | BridgeMap C++ infrastructure |
| 1 | v1.11.8 | `find_type_compatible()` in SignatureIndex |
| 2 | v1.11.9 | Stack simulator input type tracking |
| 3 | v1.11.10 | Type-directed `substitute_call()` |
| 4 | v1.11.11 | Type-directed `grow_node()` |
| 5 | v1.11.12 | Adjacent-inverse cycle detection |
| 6 | v1.11.13 | Bridge logging integration |
| 7 | v1.11.14 | Type repair inserts bridge words |
| 8 | v1.11.15 | Integration validation |
| 9 | v1.11.16+ | Documentation, merge, tag |

---

## Risk Mitigation

**Type information sparsity:** Most words have `Unknown` type signatures. The fallback to depth-only matching (Phase 1 design) ensures the engine never gets stuck with zero candidates. As more words get concrete signatures, type filtering becomes more effective.

**Performance:** `find_type_compatible()` adds a type-check loop over candidates. The candidate set is typically < 100 words. The cost is negligible vs. the execution cost of evaluating a child program.

**Backward compatibility:** All changes are additive. Existing evolution behavior is preserved when stack types are `Unknown` (falls back to depth-only). Binary fitness mode is unchanged. Pool-restricted evolution continues to work.
