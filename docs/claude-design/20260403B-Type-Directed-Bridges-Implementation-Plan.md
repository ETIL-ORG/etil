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
**Feature branch:** Created via `scripts/branch.sh type-directed-bridges` → v1.12.0
**Each phase:** `version-bump.sh patch` → implement → build → test → commit
**Final phase:** Update docs (README.md, help.til), merge to master, tag

---

## Type Signature Coverage Assessment

The bridge system depends on words having concrete `TypeSignature` information. An audit of the current codebase revealed:

| Category | Words | Concrete Types | Coverage |
|---|---|---|---|
| Main prim_table (`primitives.cpp`) | 148 | 54 | **36.5%** |
| Sub-modules (string, array, matrix, ...) | 192 | 192 | **100%** |
| **Total primitives** | **340** | **246** | **72.4%** |
| TIL-compiled words (builtins.til, user words) | ~15+ | 0 inputs | **0% inputs** |

The 94 Unknown words in the main prim_table include the arithmetic and stack manipulation words that evolution uses most. Without concrete types on these words, `find_type_compatible()` falls back to depth-only matching — defeating the purpose of type-directed mutation.

**Key finding:** Arithmetic words (`+`, `-`, `*`, `/`, `mod`, etc.) are NOT polymorphic across all types. They accept Integer and Float only — `arith_binary()` explicitly rejects Boolean and heap types (String, Array, etc.) with an error. They are incorrectly typed as Unknown.

**Phase 0 addresses this** by backfilling concrete type information before the bridge infrastructure is built.

---

## Phase 0 — Type Signature Backfill

**Start:** `scripts/version-bump.sh patch` → v1.12.1

**Goal:** Increase type signature coverage in the main prim_table from 36.5% to ~85%+, and fix `infer_signature()` to infer input types for TIL-compiled words.

**Files:**
| File | Change |
|---|---|
| `src/core/primitives.cpp` | Update prim_table type annotations |
| `src/evolution/stack_simulator.cpp` | Fix `infer_signature()` input type inference |
| `tests/unit/core/type_signature_test.cpp` | New: validate type annotations |
| `tests/unit/evolution/stack_simulator_test.cpp` | New tests for input type inference |

### Part A — Fix Incorrect Output Types

Comparison words currently output `T::Integer` but actually return `Value(bool)`. Fix to `T::Boolean`:

| Word | Current Output | Correct Output |
|---|---|---|
| `=`, `<>`, `<`, `>`, `<=`, `>=` | Integer | **Boolean** |
| `0=`, `0<`, `0>` | Integer | **Boolean** |

Boolean literal and conversion words currently output `T::Unknown`. Fix:

| Word | Current Output | Correct Output |
|---|---|---|
| `true`, `false` | Unknown | **Boolean** |
| `not` | Unknown | **Boolean** |
| `bool` | Unknown | **Boolean** |

### Part B — Type Arithmetic Words as Numeric

Arithmetic words use `arith_binary()` which rejects Boolean and heap types (String, Array, Matrix, etc.) — only Integer and Float are accepted, with auto-promotion. These are NOT polymorphic across all types. The current `Unknown` annotations are incorrect.

Since `TypeSignature::Type` has no `Numeric` meta-type, use `T::Unknown` for inputs (preserving polymorphism between Integer and Float) but fix outputs where deterministic:

| Words | Input Change | Output Change |
|---|---|---|
| `+`, `-`, `*`, `max`, `min` | Keep Unknown (Int/Float polymorphic) | Keep Unknown (output type matches input type) |
| `/` | Keep Unknown | Keep Unknown (int/int→int, float→float) |
| `mod`, `/mod` | Keep Unknown | Keep Unknown (supports fmod) |
| `negate`, `abs` | Keep Unknown | Keep Unknown |
| `within` | Keep Unknown × 3 | Unknown → **Boolean** |
| `f~` | Keep Unknown × 3 | Already Integer → **Boolean** |

**Polymorphism gap:** Arithmetic words remain `Unknown` for inputs because they accept both Integer and Float. This means `+` will still be a candidate when the stack has Strings (where it would crash). Addressing this requires either a `Numeric` meta-type or multi-signature support — deferred to a future enhancement. The bridge system still gets most of its value from the 192 concrete-typed sub-module words being correctly excluded.

### Part C — Type Boolean/Logic Words

| Word | Current | Correct |
|---|---|---|
| `and` | `(Unknown, Unknown → Unknown)` | `(Boolean, Boolean → Boolean)` |
| `or` | `(Unknown, Unknown → Unknown)` | `(Boolean, Boolean → Boolean)` |
| `xor` | `(Unknown, Unknown → Unknown)` | `(Boolean, Boolean → Boolean)` |
| `invert` | `(Unknown → Unknown)` | `(Integer → Integer)` (bitwise NOT) |

Note: If `and`/`or`/`xor` also serve as bitwise operators on integers, they are polymorphic and should remain Unknown. Verify behavior before typing.

### Part D — Fix `infer_signature()` Input Type Inference

Currently (`stack_simulator.cpp`), `infer_signature()` hardcodes all inputs as `T::Unknown`:

```cpp
for (int i = 0; i < ast.effect.consumed; ++i) {
    sig.inputs.push_back(T::Unknown);  // ← always Unknown
}
```

Enhance to infer input types by analyzing the first word that consumes each input. If the first consumer has a concrete input type, propagate it to the word's signature.

Example: `: double dup + ;` — `dup` consumes Unknown (polymorphic), but `+` consumes Unknown too (Int/Float). Result: inputs = `{Unknown}`. This is correct — `double` IS polymorphic.

Example: `: str-pair dup s+ ;` — `dup` consumes Unknown, but `s+` consumes `{String, String}`. The value from `dup` flows to `s+` which needs String. Result: inputs = `{String}`.

### Part E — Type Execution Token Words

| Word | Current | Correct |
|---|---|---|
| `'` (tick) | `(→ Unknown)` | `(→ Xt)` |
| `execute` | `(Unknown →)` | `(Xt →)` |
| `xt?` | `(Unknown → Integer)` | `(Xt → Boolean)` |
| `xt-body` | `(Unknown → Unknown)` | `(Xt → DataRef)` |

### Coverage After Phase 0

| Category | Before | After | Change |
|---|---|---|---|
| Main prim_table concrete inputs | 54 / 148 (36.5%) | ~110 / 148 (~74%) | +56 words |
| Main prim_table concrete outputs | ~80 / 148 (~54%) | ~130 / 148 (~88%) | +50 words |
| TIL-compiled word inputs | 0% | Partial (inferred) | New capability |
| **Total concrete inputs** | **246 / 340 (72.4%)** | **~302 / 340 (~89%)** | **+16.5%** |

**Unit tests:**
- Verify `=` output type is Boolean (not Integer)
- Verify `true` output type is Boolean
- Verify `and` input/output types (Boolean or Unknown depending on polymorphism check)
- Verify `invert` typed as Integer → Integer
- Verify `'` output type is Xt
- Verify `execute` input type is Xt
- Verify `infer_signature()` on `: test dup s+ ;` infers String input
- Verify `infer_signature()` on `: test dup + ;` infers Unknown input (correctly polymorphic)
- Count concrete-typed words in dictionary, verify ≥ 89%

**Commit:** `v1.12.1 — Type signature backfill for bridge system`

---

## Phase 1 — Bridge Map C++ Infrastructure

**Start:** `scripts/version-bump.sh patch` → v1.12.2

**Goal:** Build the bridge directed graph in C++ from `evolve-bridge` calls. No persistence — the graph is rebuilt from TIL at load time. No dictionary metadata — `BridgeMap` is the single source of truth.

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/bridge_map.hpp` | New: `BridgeMap` class |
| `src/evolution/bridge_map.cpp` | New: implementation with construction logging |
| `src/core/primitives.cpp` | `prim_evolve_bridge` populates `BridgeMap::add()` instead of dictionary metadata |
| `include/etil/evolution/evolution_engine.hpp` | Add `BridgeMap` member, accessor |
| `tests/unit/evolution/bridge_map_test.cpp` | New: unit tests |
| `CMakeLists.txt` | Add new source files |

### Data Flow

```
TIL startup                       C++ runtime
===========                        ===========

data/library/evolution.til         EvolutionEngine
  │                                  │
  ├── evolve-bridge                  ├── BridgeMap (in-memory directed graph)
  │   "int->float"                   │   ├── Integer → Float [int->float]
  │   "integer" "float"              │   ├── Integer → String [number->string]
  │        │                         │   ├── Array → Integer [array-length]
  │        └── prim_evolve_bridge ──→│   └── ... (22 edges total)
  │            calls BridgeMap::add()│
  │                                  │
  └── (22 calls total)               └── Ready for mutation operators
```

**No persistence.** The bridge map is rebuilt from `evolve-bridge` calls every time the evolution library is loaded — same pattern as `register_primitives()` rebuilding the dictionary.

**No dictionary metadata.** Remove the `set_concept_metadata("bridge-from"/bridge-to")` calls from `prim_evolve_bridge`. The `BridgeMap` directed graph is the single source of truth.

### Design

```cpp
namespace etil::evolution {

/// Type conversion edge in the bridge graph.
struct BridgeEdge {
    TypeSignature::Type from;
    TypeSignature::Type to;
    std::string word;  // the conversion word (e.g., "int->float")
};

/// Directed graph of type conversions built from evolve-bridge calls.
/// No persistence — rebuilt from TIL at load time.
class BridgeMap {
public:
    /// Register a single conversion. Logs at Bridge category.
    void add(TypeSignature::Type from, TypeSignature::Type to, const std::string& word);

    /// Signal that construction is complete. Logs summary.
    void finalize();

    /// Direct conversions from a given type.
    const std::vector<BridgeEdge>& conversions_from(TypeSignature::Type from) const;

    /// Find a single-hop bridge word: from → to. Returns empty if none.
    std::vector<std::string> find_bridge(TypeSignature::Type from, TypeSignature::Type to) const;

    /// Find a multi-hop path (BFS, max 2 hops). Returns ordered word sequence.
    std::vector<std::string> find_path(TypeSignature::Type from, TypeSignature::Type to, size_t max_hops = 2) const;

    /// Is there any conversion from this type?
    bool has_conversions(TypeSignature::Type from) const;

    /// Summary string for logging/diagnostics.
    std::string summary() const;

    size_t size() const;
    size_t source_type_count() const;
};

} // namespace etil::evolution
```

### Construction Logging

Each `evolve-bridge` call logs the edge being added:
```
[Bridge] registered: int->float (Integer → Float)
[Bridge] registered: array-length (Array → Integer)
[Bridge] registered: sjoin (Array → String)
...
```

After all `evolve-bridge` calls, `finalize()` (called from TIL or automatically on first query) logs the graph summary:
```
[Bridge] construction complete: 22 edges, 10 source types
[Bridge]   Integer → Float(1), String(1)
[Bridge]   Float → Integer(1), String(1)
[Bridge]   Array → Integer(1), String(1), Matrix(1)
[Bridge]   String → Integer(2), Array(1), ByteArray(1)
[Bridge]   Matrix → Float(4), Integer(2), Array(1), Json(1)
[Bridge]   ByteArray → String(1), Integer(1)
[Bridge]   Json → String(1), Array(1), Map(1)
[Bridge]   Map → Json(1), Array(2), Integer(1)
```

Logging uses the existing `EvolveLogCategory::Bridge` category. When bridge logging is off, construction is silent.

### `prim_evolve_bridge` Change

Before (current):
```cpp
dict->set_concept_metadata(word, "bridge-from", MetadataFormat::Text, std::move(from_type));
dict->set_concept_metadata(word, "bridge-to", MetadataFormat::Text, std::move(to_type));
```

After:
```cpp
auto type_from = parse_sig_type(from_type);  // "integer" → T::Integer
auto type_to = parse_sig_type(to_type);      // "float" → T::Float
engine->bridge_map().add(type_from, type_to, word);
```

A `parse_sig_type()` helper maps TIL type name strings (`"integer"`, `"float"`, `"string"`, ...) to `TypeSignature::Type` enum values. Returns `T::Unknown` for unrecognized names (logged as a warning).

**Unit tests:**
- Build graph with 22 edges, verify `size() == 22`, `source_type_count() == 10`
- `find_bridge(Integer, Float)` returns `{"int->float"}`
- `find_bridge(Integer, Integer)` returns empty (no self-loop)
- `find_bridge(Array, Float)` returns empty (no direct edge)
- `find_path(Array, Float, 2)` returns `{"array-length", "int->float"}` (2-hop)
- `find_path(Integer, Integer, 2)` returns empty (no useful path)
- `conversions_from(Matrix)` returns 4 edges (Float, Integer, Array, Json)
- `has_conversions(Unknown)` returns false
- `summary()` returns non-empty string with edge count and type breakdown
- Construction log entries present when Bridge logging is on
- Construction silent when Bridge logging is off
- `parse_sig_type("integer")` → `T::Integer`, `parse_sig_type("garbage")` → `T::Unknown` + warning

**Commit:** `v1.12.2 — Add BridgeMap C++ infrastructure with construction logging`

---

## Phase 2 — SignatureIndex Type Filtering

**Start:** `scripts/version-bump.sh patch` → v1.12.3

**Goal:** Add `type_compatible()` and `find_type_compatible()` to `SignatureIndex` — filter candidates by input types using a promotion-aware compatibility matrix, not just stack depth.

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/signature_index.hpp` | Add `type_compatible()`, `find_type_compatible()` |
| `src/evolution/signature_index.cpp` | Implement compatibility matrix and type-compatible filtering |
| `tests/unit/evolution/signature_index_test.cpp` | New tests for type compatibility and filtering |

### Type Compatibility Matrix

Strict equality is too rigid. Integer on the stack when a word wants Float should be compatible — `arith_binary()` already auto-promotes Integer→Float. The compatibility function encodes these promotion rules:

```cpp
/// Can a value of stack_type be passed to a word expecting word_input_type?
/// Encodes widening promotions (Integer→Float) but not narrowing (Float→Integer).
bool type_compatible(TypeSignature::Type stack_type, TypeSignature::Type word_input_type);
```

| Stack Type | Word Wants | Compatible? | Reason |
|---|---|---|---|
| *any* | Unknown | **Yes** | Word accepts anything |
| Unknown | *any* | **Yes** | Stack type unknown, assume compatible |
| Integer | Integer | Yes | Exact |
| Integer | Float | **Yes** | Widening promotion (auto-promoted by `arith_binary`) |
| Integer | String | No | Needs explicit bridge |
| Float | Float | Yes | Exact |
| Float | Integer | **No** | Narrowing — needs explicit `float->int` bridge |
| Boolean | Integer | **No** | Undefined behavior — not compatible |
| Boolean | Boolean | Yes | Exact |
| All other pairs | — | **Exact match only** | No implicit conversion |

The key asymmetry: **Integer→Float is safe (widening), Float→Integer is not (narrowing).** This mirrors how the runtime actually behaves.

### `find_type_compatible()`

```cpp
/// Find words matching (consumed, produced) whose input types are compatible
/// with the given stack types (using the promotion-aware compatibility matrix).
/// Falls back to find_compatible() if stack_types is empty or all Unknown.
std::vector<std::string> find_type_compatible(
    int consumed, int produced,
    const std::vector<TypeSignature::Type>& stack_types) const;
```

A candidate word is type-compatible if `type_compatible(stack_types[i], word.inputs[i])` is true for all input positions. If no type-compatible candidates exist, fall back to depth-only (never return empty).

**Unit tests:**

`type_compatible()` direct tests:
- `type_compatible(Integer, Float)` → true (widening)
- `type_compatible(Float, Integer)` → false (narrowing)
- `type_compatible(Boolean, Integer)` → false (undefined)
- `type_compatible(Integer, Integer)` → true (exact)
- `type_compatible(String, String)` → true (exact)
- `type_compatible(Integer, String)` → false (different domains)
- `type_compatible(Unknown, Float)` → true (unknown stack)
- `type_compatible(Integer, Unknown)` → true (word accepts anything)
- `type_compatible(Array, Integer)` → false (needs bridge)

`find_type_compatible()` integration tests:
- Register `+` (Unknown, Unknown → Unknown) and `copy-file` (String, String → Integer)
- `find_type_compatible(2, 1, {Integer, Integer})` includes `+`, excludes `copy-file`
- `find_type_compatible(2, 1, {Integer, Integer})` includes `pow` (Unknown, Unknown → Float) — word takes Unknown, compatible with Integer stack
- `find_type_compatible(2, 1, {String, String})` includes both (`+` is Unknown = permissive, `copy-file` is String = exact)
- `find_type_compatible(2, 1, {Integer, Float})` includes `+` (Unknown inputs), includes `pow` (Unknown inputs)
- `find_type_compatible(2, 1, {Float, Float})` includes `+` (Unknown), excludes `lshift` (Integer, Integer) — Float→Integer is narrowing
- `find_type_compatible(2, 1, {Boolean, Boolean})` includes `and` (Boolean), excludes `+` if typed, excludes `copy-file` (String)
- `find_type_compatible(2, 1, {Unknown, Unknown})` includes all (Unknown is permissive)
- `find_type_compatible(2, 1, {})` falls back to `find_compatible()` (all candidates)
- Verify fallback: if no type-compatible match, returns depth-only results

**Commit:** `v1.12.3 — Add type_compatible() and find_type_compatible() to SignatureIndex`

---

## Phase 3 — Stack Simulator Input Type Tracking

**Start:** `scripts/version-bump.sh patch` → v1.12.4

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

**Commit:** `v1.12.4 — Stack simulator tracks input types at each AST node`

---

## Phase 4 — Type-Directed Substitute

**Start:** `scripts/version-bump.sh patch` → v1.12.5

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
- Substitute in `dup +` with Integer stack: `+` replaced by `*`, `-`, `mod` (all numeric), never by `copy-file`, `s+`
- Substitute with pool restriction: type filtering applies within the pool
- Substitute with Unknown types: falls back to depth-only (existing behavior preserved)
- Substitute where no type-compatible match exists: falls back gracefully

**Commit:** `v1.12.5 — Type-directed substitute_call()`

---

## Phase 5 — Type-Directed Grow

**Start:** `scripts/version-bump.sh patch` → v1.12.6

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

**Commit:** `v1.12.6 — Type-directed grow_node()`

---

## Phase 6 — Cycle Detection

**Start:** `scripts/version-bump.sh patch` → v1.12.7

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

**Commit:** `v1.12.7 — Adjacent-inverse bridge cycle detection`

---

## Phase 7 — Bridge Logging

**Start:** `scripts/version-bump.sh patch` → v1.12.8

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

**Commit:** `v1.12.8 — Bridge logging integration`

---

## Phase 8 — Type Repair Enhancement

**Start:** `scripts/version-bump.sh patch` → v1.12.9

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

**Commit:** `v1.12.9 — Type repair inserts bridge words`

---

## Phase 9 — Integration Validation

**Start:** `scripts/version-bump.sh patch` → v1.12.10

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

**Commit:** `v1.12.10 — Integration validation for type-directed bridges`

---

## Phase 10 — Documentation and Merge

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

5. **Ask permission**, then run super-push.sh targeting the CI server (builds, tests, merges, tags, pushes — one shot). See `docs/claude-knowledge/20260403A-feature-branch-workflow.md` for details.

6. Wait for CI build to pass

7. **Ask permission**, then run `scripts/github-push.sh`

---

## Version Summary

| Phase | Version | Description |
|---|---|---|
| — | v1.11.5 | Master before feature branch |
| branch | v1.12.0 | Feature branch created (minor bump, patch reset) |
| 0 | v1.12.1 | Type signature backfill |
| 1 | v1.12.2 | BridgeMap C++ infrastructure |
| 2 | v1.12.3 | `find_type_compatible()` in SignatureIndex |
| 3 | v1.12.4 | Stack simulator input type tracking |
| 4 | v1.12.5 | Type-directed `substitute_call()` |
| 5 | v1.12.6 | Type-directed `grow_node()` |
| 6 | v1.12.7 | Adjacent-inverse cycle detection |
| 7 | v1.12.8 | Bridge logging integration |
| 8 | v1.12.9 | Type repair inserts bridge words |
| 9 | v1.12.10 | Integration validation |
| 10 | v1.12.11+ | Documentation, merge, tag |

---

## Known Limitations

### Polymorphic Arithmetic Gap

Arithmetic words (`+`, `-`, `*`, `/`, `mod`, `negate`, `abs`, `max`, `min`) accept both Integer and Float but reject String, Array, and other heap types. They crash on non-numeric inputs. However, `TypeSignature::Type` has no `Numeric` meta-type, so these words remain `Unknown` for inputs after Phase 0.

**Impact:** When the stack has `[String, String]` and `find_type_compatible()` looks for (2,1) candidates, `+` (Unknown, Unknown → Unknown) will match — but `+` on Strings crashes. This is a residual crash source.

**Mitigation:** The bridge system still eliminates the majority of cross-domain crashes because the 192 sub-module words (which have 100% concrete types) are correctly excluded. The arithmetic-on-wrong-type scenario is a smaller crash source than the current `copy-file`-replaces-`+` problem.

**Future fix:** Add a `Numeric` meta-type to `TypeSignature::Type`, or support multiple signatures per word (overloaded signatures). Either approach requires changes to the `TypeSignature` struct and all consumers.

---

## Risk Mitigation

**Type information sparsity:** Phase 0 raises coverage from 72.4% to ~89%. The fallback to depth-only matching (Phase 2 design) ensures the engine never gets stuck with zero candidates. As more words get concrete signatures, type filtering becomes more effective.

**Performance:** `find_type_compatible()` adds a type-check loop over candidates. The candidate set is typically < 100 words. The cost is negligible vs. the execution cost of evaluating a child program.

**Backward compatibility:** All changes are additive. Existing evolution behavior is preserved when stack types are `Unknown` (falls back to depth-only). Binary fitness mode is unchanged. Pool-restricted evolution continues to work.

---

## Future Paths

### Adaptive Bridge Weights (Backpropagation of Mutation Success)

Each `BridgeEdge` carries a weight (probability). When a mutation operator selects a bridge, it draws from the weighted distribution instead of uniform random. After fitness evaluation, the weight updates based on whether the child that used that bridge improved over its parent.

```
Integer → Float  [int->float]       weight: 0.72  (frequently useful)
Integer → String [number->string]   weight: 0.28  (rarely helps in math)

Array → Integer  [array-length]     weight: 0.85
Array → String   [sjoin]            weight: 0.10
Array → Matrix   [array->mat]       weight: 0.05
```

**Update rule** — exponential moving average:
```
weight = (1 - α) × weight + α × reward
reward = 1.0 if child_fitness > parent_fitness, 0.0 otherwise
```

Or a multi-armed bandit (UCB1, Thompson sampling) for exploration/exploitation balance — the selection engine already implements these strategies for word implementations.

**What it learns:**
- Which type transitions are productive for a given problem domain (e.g., `int->float` is high-value in numeric regression but low-value in string processing)
- Which specific conversion word works best when multiple exist for the same edge (e.g., `mat-norm` vs `mat-trace` vs `mat-mean` vs `mat-sum` for Matrix→Float)
- Problem-specific bridge preferences without manual tuning

**What it doesn't need:**
- Persistence — weights reset per evolution run, same as bridge map construction
- Complex infrastructure — a `double` per `BridgeEdge`, updated in the existing fitness evaluation loop
- New selection strategies — reuses the bandit/weighted-random machinery already in `selection/`

**Prerequisite:** The base bridge system (Phases 0–9) must be working and validated before adding adaptive weights. The integration validation (Phase 9) provides the baseline metrics to measure whether adaptive weights improve convergence.

### Numeric Meta-Type

Add `Numeric` to `TypeSignature::Type` to represent "Integer or Float." This would allow arithmetic words to be typed as `(Numeric, Numeric → Numeric)` instead of `(Unknown, Unknown → Unknown)`, closing the polymorphic arithmetic gap documented in Known Limitations.

### Overloaded Signatures

Support multiple `TypeSignature` variants per word, allowing `+` to declare both `(Integer, Integer → Integer)` and `(Float, Float → Float)`. `find_type_compatible()` would match against any variant. More expressive than the Numeric meta-type but more complex to implement.

### Modular Co-Evolution

As analyzed in `20260403A-Evolution-Priority-Review.md`, modular co-evolution builds on the bridge system. With type-safe mutations ensuring all programs run, MCE's combinatorial population amplification operates on a smooth fitness gradient. See `20260325-Modular-Coevolution-Design.md` for the full design.
