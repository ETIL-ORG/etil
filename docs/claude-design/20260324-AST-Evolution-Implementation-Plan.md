# AST Evolution Improvements — Implementation Plan

**Date:** 2026-03-24
**Design Doc:** `20260324-AST-Evolution-Grow-Shrink-Design.md`
**Experiments Report:** `20260324-Evolution-Experiments-Report.md`
**Prerequisites:** None — Phase 0 is self-contained
**Status:** Planned

---

## Phase 0: Evolution Debug Logging

**Goal:** Dedicated timestamped log files for the evolution pipeline. Implement first so all subsequent phases can be debugged.

**New files:**
- `include/etil/evolution/evolve_logger.hpp`
- `src/evolution/evolve_logger.cpp`

**Modified files:**
- `include/etil/evolution/evolution_engine.hpp` — Add `LogLevel`, log config to `EvolutionConfig`
- `src/evolution/evolution_engine.cpp` — Create `EvolveLogger`, pass to operators, log generation loop
- `src/evolution/ast_genetic_ops.cpp` — Add `EvolveLogger&` param to all 4 operators, log decisions
- `src/evolution/type_repair.cpp` — Log mismatch detection and shuffle insertion
- `src/evolution/fitness.cpp` — Log per-test-case results at granular level
- `src/core/primitives.cpp` — Register `evolve-log-start`, `evolve-log-stop`, `evolve-log-dir`
- `src/CMakeLists.txt` — Add `evolve_logger.cpp` to `ETIL_EVOLUTION_SOURCES`

**Steps:**

1. Create `EvolveLogCategory` enum (14 categories) and `EvolveLogger` class in new header
2. Implement `EvolveLogger`: file open/close with `YYYYMMDDThhmmss-evolve.log` naming, `enabled()`/`granular()` inline checks, `log()`/`detail()` with timestamp prefix
3. Add `LogLevel` enum and `log_level`/`log_categories`/`log_directory` fields to `EvolutionConfig`
4. Wire `EvolveLogger` into `EvolutionEngine` — construct in `evolve_word()`, pass to `AstGeneticOps`
5. Add `EvolveLogger&` parameter to `mutate()`, `crossover()`, and all 4 internal operators in `ast_genetic_ops.cpp`. Log: operator selection, candidate word lists, substitution decisions, move source/target, control flow wrap/unwrap
6. Add logging to `type_repair.cpp`: mismatch detection, shuffle word insertion, repair success/failure
7. Add logging to `fitness.cpp`: per-test-case input/expected/actual/pass at granular level, summary at logical level
8. Register 3 TIL words: `evolve-log-start ( level mask -- )`, `evolve-log-stop ( -- )`, `evolve-log-dir ( path -- )`
9. Add `evolve_logger.cpp` to `ETIL_EVOLUTION_SOURCES` in `src/CMakeLists.txt`
10. Test: run `evolve-regression.til` with `1 0xFFFF evolve-log-start`, verify log file created with expected content
11. Test: verify `0 0 evolve-log-start` produces no file and zero overhead
12. Build debug + release, run all tests, verify no regressions

**Verification:** Re-run symbolic regression experiment with logging. The log should show exactly why mutations fail (wrong word substituted, type repair failed, etc.).

**Estimated effort:** Solo 1 day / AI-assisted 2 hours

---

## Phase 1: Semantic Tag Coverage

**Goal:** Tag ~100 core primitives with `semantic-tags` metadata so `find_tiered()` produces domain-aware substitution candidates instead of falling through to Level 3.

**Modified files:**
- `data/help.til` — Add `semantic-tags` `meta!` calls

**Steps:**

1. Add tags for arithmetic words (10): `+`, `-`, `*`, `/`, `mod`, `/mod`, `negate`, `abs`, `max`, `min` → tag `arithmetic`
2. Add tags for stack words (11): `dup`, `drop`, `swap`, `over`, `rot`, `nip`, `tuck`, `pick`, `roll`, `?dup`, `depth` → tag `stack`
3. Add tags for comparison words (9): `=`, `<>`, `<`, `>`, `<=`, `>=`, `0=`, `0<`, `0>` → tag `comparison`
4. Add tags for logic words (7): `and`, `or`, `xor`, `not`, `invert`, `lshift`, `rshift` → tag `logic`
5. Add tags for math words (16): `sqrt`, `sin`, `cos`, `tan`, `tanh`, `asin`, `acos`, `atan`, `atan2`, `log`, `log2`, `log10`, `exp`, `pow`, `ceil`, `floor` → tag `math`
6. Add tags for string words (14): all `s*` words → tag `string`
7. Add tags for array words (14): all `array-*` words → tag `array`
8. Add tags for conversion words (5): `int->float`, `float->int`, `number->string`, `string->number`, `bool` → tag `conversion`
9. Add tags for I/O words (6): `.`, `cr`, `emit`, `space`, `spaces`, `type` → tag `io`
10. Add tags for map words (8): all `map-*` words → tag `map`
11. Add tags for JSON words (14): all `json-*` words → tag `json`
12. Verify existing matrix tags (22 words) are still present
13. Build, run all tests — tags are metadata, no behavioral change
14. Re-run symbolic regression with logging to measure substitution improvement: count Level 1/2/3 selections in log, compare with pre-tag baseline

**Verification:** Log should show substitute mutations selecting `arithmetic` words for numeric functions, not `mat-inv` or `lstat`.

**Estimated effort:** Solo 3 hours / AI-assisted 30 min

---

## Phase 1b: Dynamic Tag and Bridge Registration

**Goal:** User-defined words participate in evolution via manual tagging and automatic inference.

**Modified files:**
- `src/core/primitives.cpp` — Register `evolve-tag`, `evolve-bridge`, `evolve-untag`
- `src/core/interpreter.cpp` — Add tag inference and bridge inference in `finalize_definition()`
- `include/etil/evolution/signature_index.hpp` — Update `get_tags()` to check manual then inferred
- `src/evolution/signature_index.cpp` — Implement lookup priority (manual → inferred)

**Steps:**

1. Register 3 TIL words:
   - `evolve-tag ( word-str tags-str -- )` — stores `semantic-tags` concept metadata
   - `evolve-bridge ( word-str from-type to-type -- )` — stores `bridge-from`/`bridge-to` concept metadata
   - `evolve-untag ( word-str -- )` — removes `semantic-tags` concept metadata
2. Add tag inference in `finalize_definition()`:
   - After `TypeSignature` is computed, walk the bytecode body
   - Collect semantic tags from all `WordCall` instructions
   - If >75% of tagged WordCalls share a common tag, store as `semantic-tags-inferred`
   - Store union of all tags as the inferred tag string
3. Add bridge inference in `finalize_definition()`:
   - If `TypeSignature` has exactly 1 input type and 1 output type, and they differ
   - And neither is `Unknown` or `variable_*`
   - Store `bridge-from-inferred`/`bridge-to-inferred` concept metadata
4. Update `SignatureIndex::get_tags()` — check `semantic-tags` first, fall back to `semantic-tags-inferred`
5. Update bridge map lookup — check manual keys first, fall back to inferred keys
6. Write unit tests:
   - Manual `evolve-tag` sets and retrieves tags
   - `evolve-untag` removes tags
   - `evolve-bridge` registers a bridge
   - Inferred tags: `: square dup * ;` gets inferred `arithmetic` tag
   - Inferred bridge: `: arr-len array-length ;` gets inferred `Array→Integer` bridge
   - Manual overrides inferred: set manual tag, verify inferred is ignored
   - Redefine word: inferred tags recomputed
   - Primitives: inference skipped (no bytecode body)
7. Build debug + release, run all tests

**Estimated effort:** Solo 1 day / AI-assisted 2 hours

---

## Phase 2: Grow/Shrink Operators

**Goal:** Add two new AST mutation operators that change program length.

**Modified files:**
- `include/etil/evolution/ast_genetic_ops.hpp` — Declare `grow_node()`, `shrink_node()`
- `src/evolution/ast_genetic_ops.cpp` — Implement both operators, update `mutate()` dispatch from 4 to 6 operators with configurable weights
- `include/etil/evolution/evolution_engine.hpp` — Add `max_ast_nodes`, `MutationWeights` to `EvolutionConfig`
- `include/etil/evolution/ast.hpp` — Add `count_nodes()` recursive helper
- `src/evolution/ast.cpp` — Implement `count_nodes()`
- `src/evolution/evolve_logger.cpp` — Log calls for grow/shrink decisions

**Steps:**

1. Add `count_nodes(const ASTNode&) → size_t` to `ast.hpp/cpp` — recursive count of all nodes in an AST
2. Add `MutationWeights` struct and `max_ast_nodes` to `EvolutionConfig`
3. Implement `grow_node(ASTNode& ast, EvolveLogger& log) → bool`:
   - Reject if `count_nodes(ast) >= max_ast_nodes`
   - Find all `Sequence` nodes
   - Pick random Sequence, random insertion position
   - 70% grow-word: select `(1,1)` word from dictionary (or pool if configured)
   - 30% grow-literal: random int `[-10, 10]` or float `[-1.0, 1.0]`
   - Insert node, return true
4. Implement `shrink_node(ASTNode& ast, EvolveLogger& log) → bool`:
   - Find all `Sequence` nodes with ≥2 children
   - Pick random Sequence, random removable child (WordCall or Literal only)
   - Erase child, return true
5. Update `mutate()` dispatch: weighted random over 6 operators using `MutationWeights`
6. Add logging: log grow/shrink decisions (what was inserted/removed, where, why rejected)
7. Write unit tests:
   - `grow_node` inserts into a Sequence
   - `grow_node` respects `max_ast_nodes`
   - `grow_node` on 1-node AST works
   - `shrink_node` removes from a Sequence
   - `shrink_node` refuses to shrink below 1 node
   - `shrink_node` only removes WordCall/Literal, not control flow
   - Dispatch selects all 6 operators over many calls
8. Build debug + release, run all tests
9. Re-run symbolic regression with grow/shrink enabled + logging. Verify programs grow beyond initial 2 instructions.

**Verification:** Log shows programs growing from 2 to 5-15 nodes. Type repair handles the new nodes. Some children pass test cases that the original 2-instruction seed cannot.

**Estimated effort:** Solo 2 days / AI-assisted 3-4 hours

---

## Phase 3: Sibling Pools and Bridge Maps

**Goal:** Constrain evolution to relevant words via user-specified pools and type-crossing bridge words.

**Modified files:**
- `include/etil/evolution/signature_index.hpp` — Declare `find_restricted()`, `find_bridge()`
- `src/evolution/signature_index.cpp` — Implement pool-filtered lookup and bridge map lookup
- `include/etil/evolution/evolution_engine.hpp` — Add `word_pool` and `bridge_map` to `WordEvolutionState`
- `src/evolution/evolution_engine.cpp` — Pass pools to operators
- `src/evolution/ast_genetic_ops.cpp` — Use pool in substitute and grow; use bridge map in grow for domain crossings
- `src/core/primitives.cpp` — Register `evolve-register-pool` word
- `data/library/evolution.til` or `data/help.til` — Define predefined sibling pools and bridge map

**Steps:**

1. Add `find_restricted(int consumed, int produced, const vector<string>& pool) → vector<string>` to `SignatureIndex` — filters `find_compatible()` by pool membership
2. Add `BridgeMap` typedef and `find_bridge(Type from, Type to) → vector<string>` to `SignatureIndex`
3. Add `word_pool` (vector of strings) and `bridge_map` (BridgeMap) to `WordEvolutionState` in `EvolutionEngine`
4. Modify `substitute_call()`: if pool is non-empty, pass to `find_restricted()` instead of `find_tiered()`
5. Modify `grow_node()`: when inserting a word, draw from pool if configured; when stack types indicate a domain boundary, consult bridge map
6. Register `evolve-register-pool ( word-str tests-array pool-array -- flag )` — stores pool in `WordEvolutionState`
7. Define predefined pools in TIL: `math-pool`, `matrix-pool`, `string-pool`
8. Define bridge map in TIL (`data/library/evolution.til`) — single source of truth for inspection and modification, not hardcoded in C++
9. Add logging: log pool-restricted candidate lists, bridge word insertions
10. Write unit tests:
    - `find_restricted` returns only pool members
    - `find_restricted` with empty pool returns full dictionary
    - `find_bridge` returns correct conversion words
    - `substitute_call` with pool never selects outside pool
    - `grow_node` with bridge map inserts correct bridge word at type boundary
11. Build debug + release, run all tests

**Verification:** Re-run symbolic regression with `math-pool`. Log shows only arithmetic/stack words in candidate lists. No `mat-inv`, no `lstat`, no `ssplit`.

**Estimated effort:** Solo 1.5 days / AI-assisted 2-3 hours

---

## Phase 4: Validation and Tuning

**Goal:** Prove the improvements work on the three experiment problems.

**Steps:**

1. Re-run `evolve-regression.til` with all improvements (logging + tags + grow/shrink + math pool). Target: evolve `f(x) = x² + 3x + 5` from `dup +` seed.
2. Re-run `evolve-function.til` with `math-pool`. Target: evolve `f(x,y) = x*y + x - y` from `+` seed.
3. Analyze logs:
   - Count mutation operator usage (how often is each operator selected?)
   - Count type repair rejections (what fraction of grow/shrink mutations survive repair?)
   - Count Level 1/2/3 substitution tier usage (are tags working?)
   - Measure program length distribution over generations (is bloat controlled?)
4. Tune `MutationWeights` based on log analysis
5. Tune `max_ast_nodes` — if programs bloat to 30 without improving fitness, lower it
6. If >50% of grow/shrink mutations rejected: implement type repair Option A (extend repair to insert `dup`/`drop`)
7. Test `evolve-sort.til` unchanged — verify (1+1) ES still works (no regressions)
8. Update `20260324-Evolution-Experiments-Report.md` with new results
9. Commit all changes, super push

**Estimated effort:** Solo 1 day / AI-assisted 1 hour

---

## Total Estimated Effort

| Phase | Solo Human | AI-Assisted |
|---|---|---|
| Phase 0: Evolution debug logging | 1 day | 2 hours |
| Phase 1: Semantic tags (built-ins) | 3 hours | 30 min |
| Phase 1b: Dynamic tag/bridge registration | 1 day | 2 hours |
| Phase 2: Grow/shrink operators | 2 days | 3-4 hours |
| Phase 3: Sibling pools + bridge maps | 1.5 days | 2-3 hours |
| Phase 4: Validation and tuning | 1 day | 1 hour |
| **Total** | **~7 days** | **~11 hours** |

---

## Dependencies

```
Phase 0 (logging)
    ↓
Phase 1 (semantic tags)      ← TIL only, no C++ dependency on Phase 0
    ↓                           but logging helps verify tag effectiveness
Phase 1b (dynamic tags)      ← depends on Phase 1 (needs tag vocabulary defined)
    ↓
Phase 2 (grow/shrink)        ← depends on Phase 0 (logging in operators)
    ↓
Phase 3 (pools + bridges)    ← depends on Phase 1b (bridge map uses inferred bridges)
    ↓                           and Phase 2 (grow uses bridge map)
Phase 4 (validation)         ← depends on all above
```

Phase 0 and Phase 1 can be done in parallel — they have no code dependency on each other. Phase 1 is TIL-only (help.til), Phase 0 is C++ only. Phase 1b depends on Phase 1 having established the tag vocabulary.

---

## Success Criteria

1. **Logging:** `evolve-log-start 2 0xFFFF` produces a timestamped log file showing every mutation decision, type repair action, and fitness evaluation. File sorts canonically by name.
2. **Semantic tags:** Substitute mutation selects Level 1 (same-domain) words >50% of the time for tagged words, vs ~0% before.
3. **Grow/shrink:** Programs evolve from 2 instructions to 5-15 instructions. At least some children pass test cases the seed cannot.
4. **Dynamic tags:** `: square dup * ;` automatically inferred as `arithmetic`. Manual `evolve-tag` overrides inference. `SignatureIndex` picks up new tags on rebuild.
5. **Pools:** With `math-pool`, zero non-math words appear in substitution/grow candidates.
6. **Symbolic regression:** The evolved word matches `f(x) = x² + 3x + 5` on at least 7/10 test cases within 500 generations. (Full convergence may require more, but partial convergence proves the machinery works.)
