# Stage 6 Gap Analysis + Stages 0-6 Code Review

**Date:** 2026-03-20
**Scope:** Gap analysis of Stage 6 implementation vs plan, plus full code review of all AST evolution code (Stages 0-6)
**Reference:** `20260319-AST-Evolution-Implementation-Plan.md`
**Current Version:** ETIL v1.5.1

---

## Part 1: Stage 6 Gap Analysis

### What Was Planned (from Implementation Plan)

Stage 6 called for:
- `ASTGeneticOps` class with **5 mutation operators** + 1 crossover method
- Wire into `EvolutionEngine` with `use_ast_ops` flag
- **15 specified tests** across 3 categories

### What Was Implemented

| Planned Item | Status | Notes |
|-------------|--------|-------|
| **Block substitution** | Implemented | Tiered tag matching (Level 1/2/3), weighted selection |
| **Constant perturbation** | Implemented | Gaussian noise on PushInt/PushFloat |
| **Block crossover** | Implemented | WordCall-level swap between parents |
| **Block move** | **NOT IMPLEMENTED** | Plan specified: move functional block to compatible position + repair |
| **Control flow mutation** | **NOT IMPLEMENTED** | Plan specified: wrap/unwrap if/then, duplicate/remove zero-effect blocks |
| `use_ast_ops` flag | Implemented | Default true, falls back to bytecode-level |
| Bytecode-level fallback | Implemented | `GeneticOps` retained for `use_ast_ops=false` |

### Tests: Planned vs Implemented

| Planned Test | Status |
|-------------|--------|
| SubstituteActivation (mat-relu → mat-sigmoid) | **NOT WRITTEN** — generic MutateProducesChild exists but doesn't verify specific substitution |
| SubstituteLevel2 (falls back to Level 2 tag match) | **NOT WRITTEN** |
| SubstituteNoMatch (returns false) | **NOT WRITTEN** |
| PerturbConstant (value changed, structure preserved) | Implemented (PerturbChangesValue) |
| CrossoverTwoParents | Implemented (CrossoverProducesChild) |
| CrossoverIncompatible | Implemented (CrossoverNativeReturnsNull) |
| MoveBlock | **NOT APPLICABLE** — operator not implemented |
| WrapIfThen | **NOT APPLICABLE** — operator not implemented |
| UnwrapIfThen | **NOT APPLICABLE** — operator not implemented |
| RemoveZeroEffect | **NOT APPLICABLE** — operator not implemented |
| **MutationValidityRate** (>90%) | **NOT WRITTEN** — this is the key metric that proves AST > bytecode |
| **EvolveActivationFunction** (end-to-end) | **NOT WRITTEN** — the ultimate integration test |

### Stage 6 Score: 6/10

**What works:** The core pipeline (decompile → mutate → repair → compile) is functional. Substitution and perturbation produce valid mutants. Crossover splices WordCalls between parents. The EvolutionEngine correctly dispatches to AST or bytecode operators.

**What's missing:**
1. **Block move operator** — the plan's most structurally interesting operator. Moves a functional block to a new position and relies on repair to fix type alignment. Without it, the only structural change is word substitution.
2. **Control flow mutation** — wrapping/unwrapping if/then blocks. This is what allows evolution to discover conditional computation paths.
3. **Mutation validity rate test** — the headline metric. Without this, we can't prove AST-level mutation is better than bytecode-level.
4. **End-to-end activation evolution test** — proves the full stack works: define a word, register test cases, run generations, verify the fittest implementation uses a different activation function.
5. **Specific substitution tests** — the generic "mutate produces child" tests don't verify that substitution respects tag tiers or that specific words are actually swapped.

---

## Part 2: Stages 0-6 Code Review

### A. Repeat Patterns (Refactoring Opportunities)

**A.1 Instruction copying in `genetic_ops.cpp`** — Lines 56-62, 121-126, 130-135

Three identical instruction copy loops:
```cpp
Instruction copy;
copy.op = instr.op;
copy.int_val = instr.int_val;
copy.float_val = instr.float_val;
copy.word_name = instr.word_name;
```

**Fix:** Extract `Instruction copy_instruction(const Instruction& src)` helper.

**A.2 Constant perturbation duplicated** — `ast_genetic_ops.cpp:96-114` and `genetic_ops.cpp:165-189`

Identical Gaussian noise logic for PushInt/PushFloat. Both use `std::normal_distribution<double> noise(0.0, 0.1)` with identical scale computation and application.

**Fix:** Extract shared `perturb_value(int64_t&, double&, Op, std::mt19937_64&)` into a common header.

**A.3 AST node collection pattern** — `ast_genetic_ops.cpp:27-44`

`collect_word_calls_impl` and `collect_literals_impl` are identical recursive visitors differing only in the predicate.

**Fix:** Template `collect_nodes<Pred>(ASTNode&, Pred, vector<ASTNode*>&)`.

**A.4 Stack cleanup in `fitness.cpp`** — Lines 44-47, 54-59, 88-91

Three identical `while (ctx.data_stack().size() > 0)` release loops.

**Fix:** Extract `drain_and_release(ExecutionContext&)`.

### B. Null Pointer and Safety Issues

**B.1 Stale header declarations — `ast_genetic_ops.hpp:53-54`** (CRITICAL)

```cpp
void collect_word_calls(const ASTNode& node, std::vector<ASTNode*>& out, ASTNode& root);
void collect_literals(const ASTNode& node, std::vector<ASTNode*>& out, ASTNode& root);
```

These member functions are declared but never defined. The actual implementations are free functions with `_impl` suffix in the `.cpp` file. The declarations are dead code that will cause linker errors if anyone tries to call them.

**Fix:** Remove lines 52-54 from the header.

**B.2 Decompiler malformed bytecode — `decompiler.cpp:207-218`**

`find_matching_block_end()` returns `instrs.size()` if no matching `BlockEnd` is found. The caller (`decompile_range`) then uses this as a valid end position, silently processing malformed bytecode as if the block extends to end-of-code.

**Fix:** Return `std::optional<size_t>` or log a warning when markers are unbalanced.

**B.3 `const_cast` in stack simulator — `stack_simulator.cpp:251`**

```cpp
auto& effect = const_cast<ASTNode&>(node).effect;
```

The `simulate_node` takes `const ASTNode&` but mutates it via `const_cast`. This is technically UB if the original node was declared `const`.

**Fix:** Change parameter to non-const `ASTNode&` (which it effectively is — `annotate()` already takes non-const).

### C. Missing Bounds Checks

**C.1 Type repair stack access — `type_repair.cpp:118-125`**

```cpp
if (found == 1 && stack_pos == 0 && type_stack.size() >= 2) {
    std::swap(type_stack[type_stack.size()-1], type_stack[type_stack.size()-2]);
} else if (found == 2 && stack_pos == 0 && type_stack.size() >= 3) {
```

The `size() >= 2` / `size() >= 3` checks are present — safe. But the `else` case (general roll) does not apply the type permutation to the simulated stack, leaving the type model out of sync with the actual shuffle:

```cpp
// For roll: more complex, but the type at found_pos moves to TOS
needs_repair = true;
```

This is a **logic bug**: after inserting a `roll` instruction, the simulated `type_stack` is not updated to reflect the roll's effect. Subsequent type checks in the same word will operate on stale type state.

**Fix:** Apply the roll permutation to `type_stack` after inserting the shuffle nodes.

**C.2 Decompiler `end` parameter not validated — `decompiler.cpp:23`**

`decompile_range(instrs, start, end)` trusts that `end <= instrs.size()`. If `find_matching_block_end` returns a value past the end of the instruction vector, subsequent access is UB.

**Fix:** Add `end = std::min(end, instrs.size())` at the top of `decompile_range`.

### D. Security and DoS Concerns

**D.1 Fitness evaluation stack depth unlimited — `fitness.cpp:23`**

```cpp
ctx.set_limits(instruction_budget, SIZE_MAX, SIZE_MAX, 10.0);
```

Stack depth is `SIZE_MAX` — a mutant that pushes millions of values will exhaust memory before the instruction budget catches it. Each stack push is ~16 bytes (Value is POD); 100K pushes = 1.6 MB; 10M pushes = 160 MB.

**Fix:** Set `max_stack_depth` to a reasonable value (e.g., 10,000):
```cpp
ctx.set_limits(instruction_budget, 10000, SIZE_MAX, 10.0);
```

**D.2 AST recursive descent — `decompiler.cpp`, `ast_compiler.cpp`, `stack_simulator.cpp`**

All three modules use recursive function calls to process nested AST structures. A pathological word with 1000 nested if/then blocks would produce 1000 recursive calls, potentially exhausting the C++ stack.

**Mitigation:** In practice, bytecode is limited by the instruction budget (~256 instructions max from `MutationConfig::max_bytecode_length`), and each control structure needs at least 3-4 instructions. So nesting depth is bounded to ~64 levels. This is within normal C++ stack limits.

**D.3 Signature index rebuild cost — `signature_index.cpp`**

`rebuild()` iterates all dictionary words (~319) and queries metadata for each. This is O(N) with dictionary locking. Called once per `ASTGeneticOps` construction and not cached across mutations.

**Fix:** Add generation check to skip redundant rebuilds:
```cpp
void SignatureIndex::rebuild(const Dictionary& dict) {
    if (dict.generation() == generation_) return;  // already up to date
    ...
}
```

Wait — this is already in place! `generation_` is set on rebuild and `generation()` accessor exists. But `ASTGeneticOps::rebuild_index()` calls it unconditionally. The callers should check `index_.generation() != dict_.generation()` before calling.

**D.4 No size limit on test case arrays — `primitives.cpp` (evolve-register)**

`prim_evolve_register` iterates the entire test array without a size cap. A malicious TIL program could register millions of test cases, causing `evolve_word()` to run for hours.

**Fix:** Cap test case count (e.g., 1000):
```cpp
if (arr->length() > 1000) {
    ctx.err() << "Error: evolve-register max 1000 test cases\n";
    ...
}
```

### E. Dead Code

**E.1 Stale helper declarations — `ast_genetic_ops.hpp:52-54`**

As noted in B.1, `collect_word_calls` and `collect_literals` are declared but never defined. The actual implementations are `collect_word_calls_impl` and `collect_literals_impl` as free functions.

**E.2 Commented-out debug notes — `stack_simulator.cpp:255-271`**

```cpp
    // Simplify: net stack change = final - starting.
    // consumed = external items pulled from below.
    // produced = what we added above starting depth = final - starting + consumed
    // Wait, that's still wrong. Let me think differently:
    //
    // If we start with 0 items (depth_before=0, initial_depth=0):
    //   dup + : need 1 from caller (initial_depth=-1), end with 1 on stack
    //   consumed=1, produced=1
    //
    // final_depth=1, starting_depth=0, external_consumed=1
    // produced should be final_depth = 1
    // consumed should be external_consumed = 1
```

17 lines of stream-of-consciousness debugging notes left in production code.

**Fix:** Remove the commented-out reasoning. Keep only the final formula.

### F. Logic Bugs

**F.1 Type repair doesn't update type stack after roll — `type_repair.cpp:117-125`**

When the repair inserts a `swap` or `rot`, the simulated `type_stack` is updated (lines 118-125). But when it inserts a general `N roll` (line 115), the type stack update is missing:

```cpp
// For roll: more complex, but the type at found_pos moves to TOS
needs_repair = true;
```

After the `roll` instruction is inserted, the type stack should have the value at `found_pos` moved to TOS. Without this update, all subsequent type checks in the same word operate on stale state and may insert unnecessary or incorrect shuffle instructions.

**F.2 Block crossover is WordCall-only — `ast_genetic_ops.cpp:118-161`**

The plan specified "Replace a subtree from parent A with a compatible subtree from parent B." The implementation only swaps individual `WordCall` nodes, not subtrees (Sequences, control flow blocks, etc.). This means crossover can swap `mat-relu` with `mat-sigmoid` (single word), but cannot swap `[mat*, mat-add-col, mat-relu]` with `[mat-hadamard, mat-sigmoid]` (functional block).

This is a significant deviation from the plan's intent.

---

## Part 3: Prioritized Fix List

### P0 — Fix Before Next Release

| # | Issue | File | Lines | Impact |
|---|-------|------|-------|--------|
| 1 | Remove stale header declarations | `ast_genetic_ops.hpp` | 52-54 | Linker error if called |
| 2 | Remove debug comments | `stack_simulator.cpp` | 255-271 | Code clarity |
| 3 | Fix roll type stack update | `type_repair.cpp` | 117-125 | Logic bug: stale type state after roll |
| 4 | Add stack depth limit to fitness | `fitness.cpp` | 23 | DoS: unbounded stack growth |
| 5 | Cap test case count in evolve-register | `primitives.cpp` | evolve-register | DoS: unbounded iteration |

### P1 — Address in Next Stage

| # | Issue | File | Impact |
|---|-------|------|--------|
| 6 | Extract instruction copy helper | `genetic_ops.cpp` | DRY: 3 copies → 1 function |
| 7 | Extract stack drain helper | `fitness.cpp` | DRY: 3 copies → 1 function |
| 8 | Extract perturb helper | `ast_genetic_ops.cpp` + `genetic_ops.cpp` | DRY: identical logic |
| 9 | Template AST node collector | `ast_genetic_ops.cpp` | DRY: 2 identical visitors |
| 10 | Validate decompiler `end` parameter | `decompiler.cpp` | Safety: bounds check |
| 11 | Remove `const_cast` from simulator | `stack_simulator.cpp` | UB risk |

### P2 — Missing Operators and Tests

| # | Gap | Priority | Effort |
|---|-----|----------|--------|
| 12 | Block move operator | Medium | ~80 lines |
| 13 | Control flow mutation operator | Medium | ~80 lines |
| 14 | Subtree-level crossover (not just WordCall) | High | ~60 lines |
| 15 | Mutation validity rate test | **High** | ~40 lines |
| 16 | End-to-end activation evolution test | **High** | ~60 lines |
| 17 | Specific substitution tests (tag tiers) | Medium | ~50 lines |
| 18 | Decompiler populate semantic tags on WordCall nodes | Low | ~20 lines |

---

## Part 4: Acceptance Criteria Status

From the Implementation Plan's success criteria:

| Criterion | Status | Evidence |
|-----------|--------|---------|
| Round-trip fidelity for all 14 patterns | **PASS** | 14 round-trip tests in `test_ast_compiler.cpp` |
| Compile-time type inference for Category 1 | **PARTIAL** | `StackSimulator::infer_signature()` exists but not wired into compiler's `;` handler |
| Type error detection at compile time | **PARTIAL** | Simulator detects errors, but not integrated into compilation pipeline |
| Mutation validity rate >90% | **UNKNOWN** | Test not written |
| Repair effectiveness >80% | **UNKNOWN** | Not measured |
| Semantic substitution by tag tier | **IMPLEMENTED** | `find_tiered()` with weighted selection |
| Evolution efficiency vs bytecode | **UNKNOWN** | No comparative benchmark |
| Zero regressions | **PASS** | 1353/1353 tests pass |
