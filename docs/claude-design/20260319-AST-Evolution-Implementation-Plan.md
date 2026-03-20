# AST-Level Evolution: Implementation Plan

**Date:** 2026-03-19
**Reference:** `20260319-AST-Level-Evolution-Design.md`
**Current Version:** ETIL v1.3.1
**Scope:** 7 stages, each independently testable and mergeable

---

## Overview

The AST-level evolution system replaces bytecode-level random mutation with structure-aware, type-directed program transformation. The implementation is organized into 7 stages that build on each other, with each stage producing a working, tested deliverable that can be merged to master independently.

| Stage | Name | Depends On | Estimated Lines | Version Bump |
|-------|------|-----------|----------------|--------------|
| 0 | Marker Opcodes | Nothing | ~60 | Patch |
| 1 | AST Types + Decompiler | Stage 0 | ~260 | Patch |
| 2 | AST Compiler | Stage 0 | ~350 | Patch |
| 3 | Stack Simulator + Type Inference | Stages 1, 2 | ~400 | Minor |
| 4 | Type-Directed Repair | Stage 3 | ~310 | Patch |
| 5 | Semantic Tags + Signature Index | Stage 3 | ~200 | Patch |
| 6 | AST Genetic Operators + Integration | Stages 1-5 | ~510 | Minor |
| **Total** | | | **~2090** | |

---

## Stage 0: Marker Opcodes

### Goal

Add no-op marker opcodes to the compiler so that bytecode carries explicit structural hints. Zero behavioral change — markers are skipped by the inner interpreter.

### Changes

**`include/etil/core/compiled_body.hpp`** — Add 3 opcodes and 1 enum:

```cpp
// Add to Instruction::Op enum:
BlockBegin,       // No-op marker: start of control structure. int_val = BlockKind
BlockEnd,         // No-op marker: end of control structure. int_val = BlockKind
BlockSeparator,   // No-op marker: boundary within structure (else). int_val = BlockKind

// New enum:
enum class BlockKind : int64_t {
    IfThen = 1, IfThenElse = 2, DoLoop = 3, DoPlusLoop = 4,
    BeginUntil = 5, BeginWhileRepeat = 6, BeginAgain = 7,
};
```

**`src/core/compiled_body.cpp`** — Add no-op handler:

```cpp
case Instruction::Op::BlockBegin:
case Instruction::Op::BlockEnd:
case Instruction::Op::BlockSeparator:
    ++ip;
    break;
```

**`src/core/control_flow_handlers.cpp`** — Emit markers in each handler:

| Handler | Markers emitted |
|---------|----------------|
| `handle_if` | `BlockBegin(IfThen)` before `BranchIfFalse` |
| `handle_else` | Change `BlockBegin` to `IfThenElse`; emit `BlockSeparator(IfThenElse)` before `Branch` |
| `handle_then` | `BlockEnd(IfThen or IfThenElse)` |
| `handle_do` | `BlockBegin(DoLoop)` before `DoSetup` |
| `handle_loop` | `BlockEnd(DoLoop)` after `DoLoop` |
| `handle_plus_loop` | `BlockEnd(DoPlusLoop)` after `DoPlusLoop`; change `BlockBegin` to `DoPlusLoop` |
| `handle_begin` | `BlockBegin(BeginUntil)` (default; changed later if while/again) |
| `handle_until` | `BlockEnd(BeginUntil)` after `BranchIfFalse` |
| `handle_while` | Change `BlockBegin` to `BeginWhileRepeat`; emit `BlockSeparator` |
| `handle_repeat` | `BlockEnd(BeginWhileRepeat)` after `Branch` |
| `handle_again` | Change `BlockBegin` to `BeginAgain`; `BlockEnd(BeginAgain)` after `Branch` |

**`src/core/primitives.cpp`** — Update `prim_see` / `format_instruction` to display or hide markers.

### Tests

- All 1287 existing tests pass unchanged (markers are no-ops)
- New test: compile a word with each control flow pattern, verify markers present at expected positions via bytecode inspection
- New test: `see` output for a word with markers (verify human-readable format)
- **~15 new tests** in `test_compiled_body.cpp`

### Acceptance Criteria

- Zero behavioral change: all existing tests pass
- Every control flow pattern emits matched `BlockBegin`/`BlockEnd` pairs
- `see` displays markers in verbose mode, hides them by default

---

## Stage 1: AST Types + Decompiler

### Goal

Define the AST node types and implement a decompiler that converts marker-annotated bytecode into an AST.

### New Files

**`include/etil/evolution/ast.hpp`** (~80 lines)

- `ASTNodeKind` enum (Literal, WordCall, Sequence, IfThen, IfThenElse, DoLoop, DoPlusLoop, BeginUntil, BeginWhileRepeat, BeginAgain, PrintString, PushXt, ToR, FromR, FetchR, DoI, DoJ, Leave, Exit, PushDataPtr, SetDoes)
- `ASTNode` struct with kind, literal values, word_name, children vector, StackEffect, source IP range, category, semantic_tags
- Helper: `ast_to_string()` for debug printing

**`include/etil/evolution/decompiler.hpp`** (~30 lines)

- `Decompiler` class with `decompile(const ByteCode&) -> ASTNode`

**`src/evolution/decompiler.cpp`** (~150 lines)

- Marker-based recursive descent: `BlockBegin` opens a node, `BlockSeparator` splits branches, `BlockEnd` closes
- `find_matching_block_end()` scans for matching `BlockEnd` at same nesting depth
- `find_separator()` finds `BlockSeparator` within a block
- Branch/loop instructions skipped (structure is in markers)
- Literal, Call, PushXt, stack-manipulation instructions become leaf nodes

### Tests (~150 lines in `test_decompiler.cpp`)

**Structure tests** — decompile each pattern, verify AST node kinds and children:

| Test | Input | Expected AST |
|------|-------|-------------|
| Linear | `: t dup + ;` | Sequence[WordCall("dup"), WordCall("+")] |
| IfThen | `: t 0> if 1 + then ;` | Sequence[..., IfThen[Sequence[...]]] |
| IfThenElse | `: t 0> if 1 + else 1 - then ;` | Sequence[..., IfThenElse[then_body, else_body]] |
| DoLoop | `: t 10 0 do i loop ;` | Sequence[..., DoLoop[Sequence[DoI]]] |
| DoPlusLoop | `: t 10 0 do i 2 +loop ;` | Sequence[..., DoPlusLoop[...]] |
| BeginUntil | `: t begin dup while 1 - repeat ;` | Sequence[BeginWhileRepeat[cond, body]] |
| BeginAgain | `: t begin 1 again ;` | Sequence[BeginAgain[body]] |
| Nested | `: t 10 0 do i 0> if i then loop ;` | DoLoop containing IfThen |
| Literals | `: t 42 3.14 true ;` | Sequence[Literal(42), Literal(3.14), Literal(true)] |
| Strings | `: t s" hello" ;` | Sequence[PushString("hello")] |
| ReturnStack | `: t >r r@ r> ;` | Sequence[ToR, FetchR, FromR] |
| Leave | `: t 10 0 do i 5 = if leave then loop ;` | DoLoop with Leave |
| Exit | `: t 0> if exit then ;` | IfThen containing Exit |

**Debug output test** — `ast_to_string()` produces readable indented output.

### Acceptance Criteria

- Every control flow pattern decompiles to the correct AST node kind
- Children are in the correct order (then before else, condition before body)
- Leaf nodes preserve literal values, word names, string content
- All existing tests pass (decompiler is a new module, no changes to existing code)

---

## Stage 2: AST Compiler

### Goal

Compile an AST back to bytecode with proper markers and backpatched branch targets. Together with Stage 1, this completes the round-trip: bytecode → AST → bytecode.

### New Files

**`include/etil/evolution/ast_compiler.hpp`** (~30 lines)

- `ASTCompiler` class with `compile(const ASTNode&) -> std::shared_ptr<ByteCode>`

**`src/evolution/ast_compiler.cpp`** (~200 lines)

- Recursive visitor pattern: each `ASTNodeKind` emits the corresponding instructions
- `BlockBegin`/`BlockEnd`/`BlockSeparator` markers emitted around control structures
- Branch target backpatching via fixup list (same pattern as `control_flow_handlers.cpp`)
- Literal nodes emit `PushInt`/`PushFloat`/`PushBool`/`PushString`/`PushJson`
- `WordCall` nodes emit `Call` instructions (no cached_impl — must be resolved at runtime)

### Tests (~150 lines in `test_ast_compiler.cpp`)

**Round-trip fidelity tests** — for each control flow pattern:

1. Compile a word via the interpreter (`: test ... ;`)
2. Decompile to AST
3. Recompile to new bytecode via `ASTCompiler`
4. Execute both with the same inputs
5. Verify identical stack results

```cpp
TEST(ASTCompilerTest, RoundTripIfThenElse) {
    interp.interpret_line(": original dup 0> if 1 + else 1 - then ;");
    auto orig_impl = dict.lookup("original");
    auto ast = decompiler.decompile(*orig_impl->get()->bytecode());
    auto new_bc = compiler.compile(ast);
    // Execute original with input 5: expect 6
    // Execute recompiled with input 5: expect 6
    // Execute original with input -3: expect -4
    // Execute recompiled with input -3: expect -4
}
```

| Test | Word | Inputs | Expected |
|------|------|--------|----------|
| Linear | `: t dup + ;` | 21 | 42 |
| IfThen | `: t dup 0> if 1 + then ;` | 5, -3 | 6, -3 |
| IfThenElse | `: t dup 0> if 1 + else negate then ;` | 5, -3 | 6, 3 |
| DoLoop | `: t 0 swap 0 do i + loop ;` | 5 | 10 |
| DoPlusLoop | `: t 0 swap 0 do i + 2 +loop ;` | 10 | 20 |
| BeginUntil | `: t begin 1 - dup 0= until ;` | 5 | 0 |
| BeginWhileRepeat | `: t begin dup 0> while 1 - repeat ;` | 5 | 0 |
| Nested | `: t 10 0 do i 2 mod 0= if i then loop ;` | (none) | 0 2 4 6 8 |
| LeaveExit | `: t 10 0 do i 5 = if leave then i loop ;` | (none) | 0 1 2 3 4 |
| ReturnStack | `: t >r r@ r> + ;` | 3 | 6 |

**Marker preservation test** — recompiled bytecode contains markers at the same structural positions as the original.

### Acceptance Criteria

- All round-trip tests produce identical results
- Recompiled bytecode contains correct markers
- All existing tests pass

---

## Stage 3: Stack Simulator + Type Inference

### Goal

Trace the type stack at every AST node boundary. Infer `TypeSignature` for colon definitions at compile time. Build a signature index for the dictionary.

### New Files

**`include/etil/evolution/stack_simulator.hpp`** (~40 lines)

- `StackSimulator` class with `annotate(ASTNode&, const Dictionary&)` — populates `StackEffect` on every node
- `infer_signature(const ASTNode&, const Dictionary&) -> TypeSignature` — compute input/output signature for a word

**`src/evolution/stack_simulator.cpp`** (~150 lines)

- Recursive visitor: maintain `vector<TypeSignature::Type>` as the simulated type stack
- For each `WordCall`, look up callee's signature, pop inputs, push outputs
- For `Sequence`: accumulate effects across children
- For `IfThenElse`: verify both branches have same net effect
- For `DoLoop`: check if loop body has net zero effect (Category 1) or net positive (Category 2)
- Stack shuffling words apply permutations to the type vector:
  - `swap`: exchange [0] and [1]
  - `rot`: rotate [0], [1], [2]
  - `dup`: duplicate [0]
  - `drop`: remove [0]
  - `over`: insert copy of [1] at [0]

**`include/etil/evolution/signature_index.hpp`** (~30 lines)

- `SignatureIndex` class with `rebuild(const Dictionary&)`, `find_compatible(consumed, produced)`, `find_exact(TypeSignature)`
- Caches category and semantic tags per word

**`src/evolution/signature_index.cpp`** (~60 lines)

- Build `map<pair<int,int>, vector<string>>` from dictionary
- Track dictionary generation for cache invalidation

### Compile-Time Type Inference Integration

**`src/core/compile_handlers.cpp`** — In `handle_semicolon()`, after finalizing the bytecode:

1. Decompile the new bytecode to AST (uses Stage 1)
2. Run `StackSimulator::infer_signature()` on the AST
3. Store the result on the `WordImpl` via `set_signature()`
4. If inference fails (Category 2/3), set `variable_inputs`/`variable_outputs` flags

**`include/etil/core/word_impl.hpp`** — Add `variable_inputs`/`variable_outputs` to `TypeSignature`.

### Tests

**Stack simulator tests** (~100 lines in `test_stack_simulator.cpp`):

| Test | Word | Expected signature |
|------|------|--------------------|
| Simple | `: t dup + ;` | `( Integer -- Integer )` |
| Two inputs | `: t + ;` | `( Integer Integer -- Integer )` |
| Matrix | `: t mat* mat-relu ;` | `( Matrix Matrix -- Matrix )` |
| Mixed types | `: t mat-rows ;` | `( Matrix -- Integer )` |
| Swap tracking | `: t swap + ;` | `( Integer Integer -- Integer )` |
| If/then balanced | `: t dup 0> if 1 + else 1 - then ;` | `( Integer -- Integer )` |
| DO loop reducing | `: t 0 swap 0 do + loop ;` | `( Integer -- Integer )` Category 1 |
| DO loop expanding | `: t 0 do i loop ;` | `( Integer -- ??? )` Category 2 |
| Execute opaque | `: t execute ;` | Category 3 |
| Constrained execute | `: t execute swap execute + ;` | Category 4 |
| Type error | `: t mat* mat-cols mat-relu ;` | Invalid (Integer fed to mat-relu) |

**Compile-time inference tests** — define words and verify `WordImpl::signature()` is populated:

```cpp
TEST(TypeInferenceTest, ColonDefinitionGetsSignature) {
    interp.interpret_line(": double dup + ;");
    auto impl = dict.lookup("double");
    auto& sig = impl->get()->signature();
    EXPECT_EQ(sig.inputs.size(), 1);
    EXPECT_EQ(sig.outputs.size(), 1);
}
```

**Signature index tests** — rebuild index, query compatible words:

```cpp
TEST(SignatureIndexTest, FindActivationFunctions) {
    SignatureIndex idx;
    idx.rebuild(dict);
    auto results = idx.find_compatible(1, 1);  // ( Matrix -- Matrix )
    // Should include mat-relu, mat-sigmoid, mat-tanh, mat-transpose, ...
}
```

### Acceptance Criteria

- All Category 1 words get correct signatures
- Category 2 words are flagged with `variable_outputs`
- Category 3 words are flagged with unknown signature
- Type errors detected (mat-cols output fed to mat-relu input)
- Stack shuffling words correctly permute the type vector
- Signature index returns all type-compatible words
- All existing tests pass

---

## Stage 4: Type-Directed Repair

### Goal

Given a mutated AST with type mismatches, deterministically insert stack shuffling instructions to fix them. If unfixable, report failure.

### New Files

**`include/etil/evolution/type_repair.hpp`** (~40 lines)

- `TypeRepair` class with `repair(ASTNode&, const Dictionary&) -> bool`
- Returns true if all mismatches resolved, false if unrepairable

**`src/evolution/type_repair.cpp`** (~120 lines)

- Walk the AST, maintain simulated type stack
- At each `WordCall`, check if TOS and below match the callee's input signature
- On mismatch: search deeper in the type stack for the needed type
- If found: compute minimal shuffle (`swap`, `rot`, or `N roll`) and insert before the call
- If not found: return false (unrepairable)
- `compute_minimal_shuffle(from_pos, to_pos) -> vector<ASTNode>`

### Tests (~150 lines in `test_type_repair.cpp`)

| Test | Input AST | Expected repair |
|------|-----------|----------------|
| No repair needed | `mat-relu` after `mat*` | No changes, return true |
| Swap needed | `obs xt` → `obs-map` (needs xt on TOS) | Insert `swap` before `obs-map` |
| Rot needed | Type at position 2 needed at TOS | Insert `rot` |
| Roll needed | Type at position 4 needed at TOS | Insert `4 roll` |
| Unrepairable | Needed type not on stack at all | Return false |
| Type error detection | `Integer` where `Matrix` needed, no Matrix on stack | Return false |
| Multiple repairs | Two mismatches in one word | Both fixed |
| Repair inside if/then | Mismatch in then-body | Repair applied in correct branch |

**Repair + round-trip test** — mutate an AST (swap two word calls), repair, compile, execute:

```cpp
TEST(TypeRepairTest, RepairAfterSubstitution) {
    // : original X W mat* mat-relu ;
    // Substitute mat-relu → mat-sigmoid (same sig, no repair needed)
    // Substitute mat-relu → mat-sum (different sig, repair inserts nothing — just fails)
}
```

### Acceptance Criteria

- Correctly identifies mismatch position and needed type
- Inserts minimal shuffling (swap preferred over rot, rot over roll)
- Returns false for genuinely unrepairable mismatches
- Repaired AST compiles and executes without stack errors
- All existing tests pass

---

## Stage 5: Semantic Tags + Signature Index Enhancement

### Goal

Add semantic tag metadata to words and enhance the signature index to support tiered substitution queries.

### Changes

**`data/help.til`** — Add `semantic-tags` metadata entries for all matrix/MLP/observable words (~50 entries).

**`src/evolution/signature_index.cpp`** — Extend to cache category and semantic tags per word. Add query methods:

```cpp
// Find words with same signature AND matching semantic tags
vector<string> find_semantically_compatible(
    const TypeSignature& sig,
    const vector<string>& required_tags) const;

// Find words by substitution level (1=exact tags, 2=overlapping, 3=sig only)
vector<pair<string, int>> find_tiered(
    const TypeSignature& sig,
    const vector<string>& tags) const;
```

**`include/etil/evolution/ast.hpp`** — Ensure `ASTNode` has `category` and `semantic_tags` fields (already in design).

**`src/evolution/decompiler.cpp`** — Populate `category` and `semantic_tags` on `WordCall` nodes from dictionary metadata during decompilation.

### Tests (~50 lines)

- Tags loaded from help.til and queryable via SignatureIndex
- `find_tiered("mat-relu")` returns Level 1: mat-sigmoid, mat-tanh; Level 2: mat-clip, mat-scale; Level 3: mat-transpose
- Decompiled AST has category and tags populated on WordCall nodes

### Acceptance Criteria

- All matrix/MLP/observable words have semantic tags
- Tiered query returns correct levels
- All existing tests pass

---

## Stage 6: AST Genetic Operators + EvolutionEngine Integration

### Goal

Implement the 5 AST-level genetic operators (using type repair) and wire them into `EvolutionEngine`.

### New Files

**`include/etil/evolution/ast_genetic_ops.hpp`** (~50 lines)

- `ASTGeneticOps` class with 5 mutation methods + 1 crossover method

**`src/evolution/ast_genetic_ops.cpp`** (~200 lines)

Five operators, each ending with a repair step:

| Operator | Creative phase | Repair phase |
|----------|---------------|-------------|
| **Block substitution** | Replace a WordCall with a semantically compatible alternative (tiered: Level 1 → 2 → 3) | Fix any type mismatches from different output types |
| **Constant perturbation** | Gaussian noise on a Literal node's value | None needed (structure unchanged) |
| **Block crossover** | Replace a subtree from parent A with a compatible subtree from parent B | Fix type mismatches at splice boundaries |
| **Block move** | Move a functional block to a different position | Fix type misalignment from reordering |
| **Control flow mutation** | Wrap/unwrap a block in if/then, duplicate/remove a zero-effect block | Verify type consistency of new structure |

### EvolutionEngine Changes

**`src/evolution/evolution_engine.cpp`** — Replace bytecode-level mutation with AST pipeline:

```cpp
// In evolve_word(), the child creation loop changes to:
auto ast = decompiler_.decompile(*parent->bytecode());
stack_simulator_.annotate(ast, dict_);

if (coin < mutation_rate) {
    ast_genetic_ops_.mutate(ast, signature_index_);  // creative + repair
} else {
    auto ast_b = decompiler_.decompile(*other_parent->bytecode());
    ast_genetic_ops_.crossover(ast, ast_b, signature_index_);  // creative + repair
}

auto new_bc = ast_compiler_.compile(ast);
child->set_bytecode(new_bc);
```

The old `GeneticOps` bytecode-level class is retained for `perturb_constant` (applied after AST compilation as a value-only tweak).

### Tests (~200 lines in `test_ast_genetic_ops.cpp`)

**Per-operator tests:**

| Test | Operator | Verify |
|------|----------|--------|
| SubstituteActivation | Block substitution | mat-relu replaced with mat-sigmoid; compiled word executes |
| SubstituteLevel2 | Block substitution | Falls back to Level 2 tag match |
| SubstituteNoMatch | Block substitution | Returns false when no compatible word exists |
| PerturbConstant | Constant perturbation | Literal value changed; structure preserved |
| CrossoverTwoParents | Block crossover | Child contains subtree from parent B; compiles and executes |
| CrossoverIncompatible | Block crossover | Returns false when no compatible subtrees |
| MoveBlock | Block move | Block relocated; repair inserts swap; executes correctly |
| WrapIfThen | Control flow mutation | Block wrapped in if/then; executes |
| UnwrapIfThen | Control flow mutation | if/then removed; body executes directly |
| RemoveZeroEffect | Control flow mutation | Zero-effect block removed; behavior preserved |

**Mutation validity rate test:**

```cpp
TEST(ASTGeneticOpsTest, MutationValidityRate) {
    // Define : double dup + ;
    // Run 100 random mutations
    // Count how many produce bytecode that executes without error
    // Assert > 90% valid (vs ~5% for bytecode-level)
}
```

**End-to-end evolution test:**

```cpp
TEST(ASTEvolutionTest, EvolveActivationFunction) {
    // Define : fwd mat* mat-relu ;
    // Register test cases that reward mat-sigmoid behavior
    // Run 10 generations
    // Verify highest-weight impl uses mat-sigmoid
}
```

### Acceptance Criteria

- All 5 operators produce compilable, executable AST mutations
- Mutation validity rate > 90%
- End-to-end evolution test passes (activation function evolved)
- All existing tests pass
- `EvolutionEngine` API unchanged (callers unaware of AST vs bytecode)

---

## Cross-Stage Dependencies

```
Stage 0: Marker Opcodes
    ↓
Stage 1: Decompiler ←──────── Stage 2: AST Compiler
    ↓                              ↓
Stage 3: Stack Simulator + Type Inference
    ↓                    ↓
Stage 4: Type Repair     Stage 5: Semantic Tags
    ↓                    ↓
Stage 6: Genetic Operators + Integration
```

Stages 1 and 2 can be developed in parallel after Stage 0.
Stages 4 and 5 can be developed in parallel after Stage 3.
Stage 6 requires all prior stages.

---

## Version Bumping Strategy

| Stage | Bump | Rationale |
|-------|------|-----------|
| 0 | Patch (v1.3.2) | No new API, internal compiler change |
| 1 | Patch (v1.3.3) | New module, no integration yet |
| 2 | Patch (v1.3.4) | New module, no integration yet |
| 3 | Minor (v1.4.0) | Compile-time type inference changes WordImpl behavior |
| 4 | Patch (v1.4.1) | New module, no integration yet |
| 5 | Patch (v1.4.2) | Metadata additions only |
| 6 | Minor (v1.5.0) | EvolutionEngine behavior change |

---

## Risk Mitigation

| Risk | Mitigation | Stage |
|------|-----------|-------|
| Markers change existing test behavior | Markers are no-ops; run full suite before merge | 0 |
| Decompiler misparses a pattern | Round-trip tests for all 14 control flow patterns | 1+2 |
| Type inference slows compilation | Inference is O(n) in bytecode length; profile if > 1ms | 3 |
| Repair inserts too many shuffles | Cap shuffle depth (e.g., max position 4); reject deeper | 4 |
| Semantic tags are incomplete | Tags are metadata in help.til; add incrementally | 5 |
| AST mutation changes EvolutionEngine API | API is preserved; only internal mutation strategy changes | 6 |

---

## Test Count Projections

| Stage | New Tests | Running Total (est.) |
|-------|-----------|---------------------|
| Current | — | 1287 |
| 0 | ~15 | ~1302 |
| 1 | ~15 | ~1317 |
| 2 | ~12 | ~1329 |
| 3 | ~20 | ~1349 |
| 4 | ~10 | ~1359 |
| 5 | ~5 | ~1364 |
| 6 | ~15 | ~1379 |

---

## Success Criteria (End State)

1. **Round-trip fidelity:** `execute(compile(decompile(bc)))` produces identical results for all 14 control flow patterns
2. **Compile-time type inference:** every Category 1 colon definition has a populated `TypeSignature`
3. **Type error detection:** invalid words are identified at compile time (no valid input signature exists)
4. **Mutation validity rate:** > 90% of AST mutations produce executable bytecode (vs ~5% bytecode-level)
5. **Repair effectiveness:** type-directed repair resolves > 80% of type mismatches via shuffle insertion
6. **Semantic substitution:** Level 1 substitutions (same tags) produce meaningful evolutionary experiments
7. **Evolution efficiency:** AST evolution reaches fitness targets in fewer generations than bytecode evolution
8. **Zero regressions:** all existing tests pass at every stage
