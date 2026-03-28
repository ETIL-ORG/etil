# Type-Directed Bridges: Eliminating the Crash Floor in AST Evolution

**Date:** 2026-03-25
**References:** `20260324-Evolution-Experiments-Report.md`, `20260324-AST-Evolution-Grow-Shrink-Design.md`, `20260325-Distance-Based-Fitness-Design.md`
**Prerequisites:** v1.9.1 (logging, tags, grow/shrink, pools, distance fitness, diff logging)
**Status:** Design

---

## Background: What the Experiments Revealed

Five phases of evolution engine improvements (v1.7.0–v1.9.1) added debug logging, semantic tags, grow/shrink operators, sibling pools, distance-based fitness, and diff/AST dump logging. These tools made the evolution pipeline fully transparent. The experiments then revealed a problem the improvements didn't solve.

### The Crash Floor

Running 100 generations of symbolic regression (`f(x) = x² + 3x + 5`) with the full dictionary (no pool restriction), the evolution log showed:

- **Fitness flatline**: 0.180 (Gen 0) → 0.107 (Gen 2) → 0.107 for 98 more generations
- **Zero type repair interventions**: 402 successful mutations, 0 rejected, 0 repair-modified
- **Cross-domain substitutions**: `+` replaced by `copy-file`, `dup` replaced by `mat-inv`, `mkdir-tmp`
- **All children crash at runtime**: fitness = 0.1 (speed component floor, zero correctness)

The substitute mutation selects replacements by *stack depth* — `(2,1)` matches `(2,1)` — regardless of the *types* those words consume and produce. `+` takes `(Integer, Integer)` but `copy-file` takes `(String, String)`. Same depth, incompatible types. Type repair doesn't catch this because it only checks depth, not types.

**Result**: 90%+ of mutations produce programs that compile but crash at runtime. All crashes score identically (fitness 0.1). Selection pressure is zero. Evolution converges on the first generation's best and never improves.

### Pool Restriction: Correct but Insufficient

Running with a math-only pool eliminates the cross-domain crashes — every substitution is `+` → `-` or `dup` → `over`, all type-compatible. But the pool creates a different problem: **isolation**. The engine can only explore math words. Programs that need type conversions (e.g., `array-length` to get an integer from an array) are unreachable.

More critically, even within the math pool, evolution stalls at Gen 6. The population of 10 converges to nearly identical programs, and every mutation is a lateral move. The pool prevents crashes but doesn't create a fitness gradient.

### Distance Fitness: Necessary but Not Sufficient

Distance-based fitness (v1.8.0) gives the seed `dup +` a score of 0.18 instead of 0.0, providing a nonzero starting gradient. But the gradient only helps if mutations produce programs that *run*. A program that crashes still scores 0.1 (the speed component floor), and a program scoring 0.12 is barely distinguishable from 0.1. The gradient exists but is drowned out by the crash floor.

---

## The Core Insight

The three improvements — pools, distance fitness, and grow/shrink — each solve part of the problem:

| Improvement | What it fixes | What it doesn't fix |
|---|---|---|
| Pools | Prevents cross-domain crashes | Creates isolation, no multi-domain programs |
| Distance fitness | Provides gradient for valid programs | Gradient invisible when 90% of programs crash |
| Grow/shrink | Enables program length exploration | New instructions are often type-incompatible |

**The missing piece**: ensure every mutation produces a *runnable* program. If zero mutations crash, the distance fitness gradient is always visible, and selection pressure is continuous.

---

## Design: Type-Directed Bridges

### The Type Graph

The bridge map (defined in `data/library/evolution.til`) is a directed graph of type conversions:

```
Integer ──→ Float      (int->float)
Integer ──→ String     (number->string)
Integer ──→ Boolean    (0=, 0<, 0>)
Float ────→ Integer    (float->int)
Float ────→ String     (number->string)
Array ────→ Integer    (array-length)
Array ────→ String     (sjoin)
Array ────→ Matrix     (array->mat)
String ───→ Integer    (slength, string->number)
String ───→ Array      (ssplit)
String ───→ ByteArray  (string->bytes)
Matrix ───→ Float      (mat-norm, mat-trace, mat-mean, mat-sum)
Matrix ───→ Integer    (mat-rows, mat-cols)
Matrix ───→ Array      (mat->array)
Map ──────→ Array      (map-keys, map-values)
Map ──────→ Integer    (map-length)
Map ──────→ Json       (map->json)
Json ─────→ String     (json-dump)
Json ─────→ Array      (json->array)
Json ─────→ Map        (json->map)
ByteArray ─→ String    (bytes->string)
ByteArray ─→ Integer   (bytes-length)
```

Multi-hop paths emerge naturally: `Array → Integer → Float` via `array-length int->float`.

### Type-Directed Mutation

The stack simulator already tracks types at each AST node. Currently this information is used only for TypeSignature inference at `;` time. The design extends it to guide mutation decisions.

**Current pipeline** (depth-only):
```
1. Pick random mutation point
2. Pick random word with matching (consumed, produced) count
3. Insert/substitute
4. Type repair checks depth balance
5. Compile
6. 90% crash at runtime → fitness 0.1
```

**Type-directed pipeline**:
```
1. Pick random mutation point
2. Run stack simulator to determine types at that position
3. Filter candidates to words whose INPUT TYPES match the stack state
4. If no direct match, consult bridge map for a conversion path
5. Insert type-legal word (or bridge + word)
6. Guaranteed runnable → distance fitness always meaningful
```

### Impact on Substitute Mutation

Current `substitute_call()` flow:
```cpp
// Find words with same (consumed, produced) stack effect
auto candidates = index_.find_compatible(consumed, produced);
// 100 candidates for (2,1): +, -, copy-file, mat-add-col, obs-take, ...
```

Type-directed flow:
```cpp
// Simulate stack types at the substitution point
auto stack_types = simulator_.types_at(ast, position);
// stack_types = [Integer, Integer] (from 'dup' duplicating an integer)

// Filter candidates by input types
auto candidates = index_.find_type_compatible(consumed, produced, stack_types);
// 15 candidates for (Integer,Integer→Integer): +, -, *, /, mod, max, min, ...
// copy-file EXCLUDED (needs String, String)
```

### Impact on Grow Mutation

Current `grow_node()` inserts a random `(1,1)` word. With type direction:

```cpp
// Stack has [Integer] at insertion point
// Find (1,1) words that accept Integer:
//   abs, negate, 1+, 1-, dup (Integer → Integer)
//   int->float (Integer → Float) — bridge to float domain
//   number->string (Integer → String) — bridge to string domain
//   0= (Integer → Boolean) — bridge to boolean domain
```

The bridge words appear naturally as type-legal options. The engine doesn't need to be told to cross domains — the type graph makes cross-domain words available whenever they're type-compatible.

### Impact on Type Repair

Current type repair inserts shuffles (`swap`, `rot`, `roll`) to fix depth mismatches. With type direction, repair also inserts **bridge words** to fix type mismatches:

```
Stack has: [Array]
Word needs: Integer
Repair: insert 'array-length' (Array → Integer)
```

This is the key connection between bridges and repair. Type repair becomes a constructive mechanism that builds type-correct programs, not just a shuffle inserter.

---

## How This Breaks Local Minima

Local minima exist because the fitness landscape has three regions:

```
Fitness
  1.0 ┤          ╭── correct programs
  0.2 ┤  ╭───── runnable-but-wrong programs (distance gradient)
  0.1 ┤──┤───── crashed programs (flat floor)
  0.0 ┤  │
      └──┴──────────────────────
         Program space →
```

Currently, 90% of mutations land in the crash floor (0.1). The gradient region (0.1–1.0) is almost unreachable by random mutation.

With type-directed bridges, the crash floor is eliminated:

```
Fitness
  1.0 ┤          ╭── correct programs
  0.2 ┤  ╭─────╯ runnable-but-wrong (smooth gradient)
  0.0 ┤──╯       (no crash floor — all programs run)
      └──────────────────────────
         Program space →
```

Every mutation produces a runnable program. Every program gets a meaningful distance score. Selection pressure is continuous. Evolution can climb the gradient.

---

## Cycle Detection in the Type Graph

The type graph is cyclic:
- `Integer → Float → Integer` via `int->float` / `float->int`
- `String → Array → String` via `ssplit` / `sjoin`
- `Map → Json → Map` via `map->json` / `json->map`

Without protection, the grow mutation could insert `int->float float->int int->float float->int` — a no-op loop that bloats the program without improving fitness.

### Three Options Evaluated

**Option A: Per-mutation visited set (no persistent state)**

Each call to `grow_node()` gets a temporary `visited_types` set. Before inserting a bridge word, check if we've already bridged FROM this type in this mutation call.

- Pro: Simple, stateless
- Con: Doesn't prevent the NEXT mutation from undoing what this one did
- Con: Only protects within a single mutation call

**Option B: AST-level cycle detection (no persistent state)**

Before inserting a bridge, scan the nearby AST for the inverse bridge. If `int->float` is at position N, don't insert `float->int` at position N±1.

- Pro: No persistent state, catches obvious round-trips
- Pro: O(N) scan is cheap on small ASTs
- Con: Doesn't catch cycles separated by intervening instructions
- Con: Doesn't prevent multi-generation oscillation

**Option C: Per-impl path history (persistent state)**

Store the sequence of bridge insertions on each `WordImpl` as metadata. Each child inherits the parent's path. Before inserting a bridge, check if the inverse transition exists in the history.

- Pro: Catches multi-generation oscillation
- Pro: Complete cycle prevention
- Con: Adds persistent state to `WordImpl`
- Con: History grows over generations, needs pruning
- Con: More complex implementation

### Recommendation

**Start with Option B.** It catches the obvious `int->float float->int` adjacent loops with zero persistent state. The `max_ast_nodes` bloat control (default 30) is the safety net — no-op conversion loops waste node budget, causing parsimony pressure to remove them.

**Monitor with the existing diff logging.** The evolution diff view clearly shows when bridge words are inserted. Patterns like repeated `int->float` / `float->int` in the diff would be immediately visible in the log. If multi-generation oscillation appears, upgrade to Option C.

**Option A is insufficient** — it protects a single mutation call but not the sequence of mutations across a generation.

---

## Relationship to Existing Components

```
┌─────────────────────────────────────────────────────┐
│                   MUTATION PIPELINE                  │
│                                                     │
│  1. Decompile → AST                                │
│  2. Stack Simulator annotates types at each node    │ ← existing
│  3. Mutation operator selects position              │
│  4. ★ Type graph filters candidates by input types  │ ← NEW
│  5. ★ Bridge map provides cross-domain options      │ ← NEW
│  6. ★ Cycle detection prevents no-op loops          │ ← NEW
│  7. Type repair inserts bridges for mismatches      │ ← ENHANCED
│  8. Compile → bytecode                             │
│  9. Distance fitness evaluates (always meaningful)  │ ← existing
│                                                     │
│  Key: ★ = new, all others exist but may need mods  │
└─────────────────────────────────────────────────────┘
```

### What Already Exists

- **Stack simulator** (`stack_simulator.cpp`): tracks `SigType` at each node, `infer_signature()`, `annotate()`
- **SignatureIndex** (`signature_index.cpp`): `find_compatible()` by depth, `find_tiered()` with tags, `find_restricted()` with pool
- **Type repair** (`type_repair.cpp`): inserts shuffles for depth mismatches, `find_type_in_stack()`
- **Bridge map** (`data/library/evolution.til`): TIL-defined type conversion table
- **Diff logging** (`evolve_logger.hpp`): shows every mutation before/after with repair column

### What Needs to Change

| Component | Change |
|---|---|
| `SignatureIndex` | Add `find_type_compatible()` that filters by input types, not just depth |
| `substitute_call()` | Use `find_type_compatible()` instead of `find_compatible()` |
| `grow_node()` | Query stack types at insertion point, select type-legal words |
| `type_repair` | Insert bridge words (from bridge map) when types mismatch, not just shuffles |
| `ast_genetic_ops` | Run stack simulator before mutation to have type annotations available |
| Bridge map | Load from TIL into C++ data structure accessible by `SignatureIndex` |

---

## Expected Impact

### Quantitative Predictions

| Metric | Current (v1.9.1) | With Type-Directed Bridges |
|---|---|---|
| Mutation crash rate | ~90% (no pool) | ~0% |
| Children with fitness > 0.1 | ~5-10% | ~95-100% |
| Generations to first improvement | Gen 2 then stalls | Continuous improvement |
| Fitness at Gen 100 | 0.107 (flatline) | Should show upward trend |

### Symbolic Regression Scenario

With type-directed bridges, the evolution of `f(x) = x² + 3x + 5` from `dup +`:

1. **Gen 0**: `dup +` (= 2x), fitness 0.18
2. **Gen N**: grow inserts literal `3`, type-legal (Integer on Integer stack) → `dup 3 +` (= x+3), fitness maybe 0.15 (different but not better for all tests)
3. **Gen N+M**: substitute `+` → `*` → `dup 3 *` (= 3x), fitness ~0.12 (closer on some tests)
4. **Gen N+M+K**: grow inserts `dup *` → `dup dup * 3 * +` (approaching x²+3x), fitness climbing

Each step is type-safe. Each step produces a runnable program. The distance fitness distinguishes each step. Selection pressure moves the population toward better programs.

Whether this actually converges in 500 generations depends on the search space size and mutation operator balance. But the key difference is: **the engine can explore freely without hitting the crash floor**.

---

## Implementation Priority

This is the **highest-impact remaining change** for the evolution engine. It transforms the system from:

- Random search with 90% rejection → Constructive search with ~0% rejection
- Flat crash floor at 0.1 → Smooth gradient from 0.0 to 1.0
- Isolated sibling pools → Connected type graph with natural cross-domain exploration

The logging infrastructure (Phases 0, diff, AST dump) is ready to validate every step of the implementation.

---

## Confirmed: Type Repair Never Fires (v1.9.3, 2026-03-28)

The diff logging R column (repair marker) has been blank across all experiments — 500 generations of neutral padding, 100 generations of no-pool full-dictionary evolution, and every test run since v1.9.0. Investigation confirmed this is not a logging bug. The logging and the `format_mutation_diff()` R-column logic are correct. Type repair genuinely never inserts shuffles.

**Root cause**: `TypeRepair::repair_sequence()` walks the AST simulating a type stack and checks each `WordCall`'s input signature against what's on the stack. When it finds a mismatch, it searches deeper in the stack for the needed type and inserts a `swap`/`rot`/`roll` to bring it to TOS. However:

1. **Math pool**: All words consume and produce `Integer`. No type mismatches are possible within the pool.

2. **Full dictionary without pool**: Substitute picks words by stack depth `(consumed, produced)`, not by type. A word like `copy-file` `(String, String → Integer)` replaces `+` `(Integer, Integer → Integer)`. The types are incompatible, but most words have `Unknown` in their `TypeSignature` (only ~120 primitives have concrete tags; user-defined words infer signatures at `;` time but many resolve to `Unknown`). At line 99 of `type_repair.cpp`, `actual == T::Unknown` → `continue` — the mismatch is invisible.

3. **The stack simulator starts empty**: The simulated `type_stack` begins with no entries. The first word's inputs are checked against an empty stack, which hits `stack_pos >= type_stack.size()` → `continue` (underflow, can't repair). So the first N words in any program are never type-checked.

**Conclusion**: The R column will populate when:
- Signatures have concrete types (not `Unknown`) — requires the type-directed bridge system to enforce type-aware mutation
- Cross-domain mutations create real type mismatches (e.g., `Integer` on stack but word needs `String`)
- The stack simulator has initial type context (e.g., from the word's own input signature)

The R column is infrastructure waiting for the bridge system. It is not broken.
