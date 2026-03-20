# Stage 6 Completion Plan: Fixes, Refactoring, Missing Operators, and Tests

**Date:** 2026-03-20
**Reference:** `20260320-Stage6-Gap-Analysis-and-Code-Review.md`
**Current Version:** ETIL v1.5.1
**Goal:** Bring Stage 6 from 6/10 to 10/10

---

## Overview

5 phases, each independently mergeable. Phases 1-2 are bug fixes and cleanup (no new features). Phase 3 adds the missing operators. Phase 4 adds the missing tests. Phase 5 wires compile-time type inference into the compiler.

| Phase | Name | Bump | Est. Lines Changed |
|-------|------|------|--------------------|
| 1 | P0 bug fixes + safety | Patch | ~40 |
| 2 | DRY refactoring | Patch | ~80 (net negative) |
| 3 | Missing operators (block move, control flow, subtree crossover) | Patch | ~200 |
| 4 | Missing tests (validity rate, end-to-end, tag tiers) | Patch | ~250 |
| 5 | Compile-time type inference in `;` handler | Minor | ~80 |

---

## Phase 1: P0 Bug Fixes + Safety

### 1.1 Remove stale header declarations

**File:** `include/etil/evolution/ast_genetic_ops.hpp` lines 52-54

Remove:
```cpp
void collect_word_calls(const ASTNode& node, std::vector<ASTNode*>& out, ASTNode& root);
void collect_literals(const ASTNode& node, std::vector<ASTNode*>& out, ASTNode& root);
```

These member functions are declared but never defined. The actual implementations are free functions `collect_word_calls_impl` and `collect_literals_impl` in the `.cpp` file.

### 1.2 Fix roll type stack update

**File:** `src/evolution/type_repair.cpp` lines 117-125

Current code inserts a `roll` instruction but does not update the simulated `type_stack`. After the roll, all subsequent type checks operate on stale state.

Fix: After the general `roll` case, apply the permutation:
```cpp
if (found_pos < type_stack.size()) {
    auto moved_type = type_stack[type_stack.size() - 1 - static_cast<size_t>(found)];
    type_stack.erase(type_stack.begin() +
        static_cast<long>(type_stack.size() - 1 - static_cast<size_t>(found)));
    type_stack.push_back(moved_type);
}
```

### 1.3 Add stack depth limit to fitness evaluation

**File:** `src/evolution/fitness.cpp` line 23

Change:
```cpp
ctx.set_limits(instruction_budget, SIZE_MAX, SIZE_MAX, 10.0);
```
To:
```cpp
ctx.set_limits(instruction_budget, 10000, SIZE_MAX, 10.0);
```

### 1.4 Cap test case count in evolve-register

**File:** `src/core/primitives.cpp` in `prim_evolve_register`

After `arr->length()` is available, before the parse loop:
```cpp
if (arr->length() > 1000) {
    ctx.err() << "Error: evolve-register max 1000 test cases\n";
    word_str->release();
    arr->release();
    ctx.data_stack().push(Value(false));
    return true;
}
```

### 1.5 Remove debug comments

**File:** `src/evolution/stack_simulator.cpp` lines 255-271

Remove the 17-line commented-out reasoning block. Keep only the final formula:
```cpp
effect.consumed = external_consumed;
effect.produced = final_depth;
effect.valid = state.valid;
```

### 1.6 Validate decompiler end parameter

**File:** `src/evolution/decompiler.cpp` top of `decompile_range`

Add:
```cpp
end = std::min(end, instrs.size());
```

### 1.7 Remove const_cast from simulator

**File:** `src/evolution/stack_simulator.cpp` line 251

Change `simulate_node` parameter from `const ASTNode& node` to `ASTNode& node` (non-const). Update the header and all call sites. The `annotate()` method already takes non-const `ASTNode&`, so this is consistent.

### Tests

- Existing 1353 tests must pass unchanged
- New test: `TypeRepairTest.RollUpdatesTypeStack` — verify type stack is correct after roll insertion
- New test: `FitnessTest.StackDepthLimited` — verify a push-heavy mutant is killed before exhausting memory
- New test: `EvolvePrimitiveTest.RegisterMaxTestCases` — verify >1000 test cases are rejected

### Acceptance Criteria

- All P0 issues resolved
- Zero regressions
- 3 new tests pass

---

## Phase 2: DRY Refactoring

### 2.1 Extract instruction copy helper

**File:** `src/evolution/genetic_ops.cpp`

Add to `genetic_ops.hpp`:
```cpp
static etil::core::Instruction copy_instruction(const etil::core::Instruction& src);
```

Replace 3 identical copy loops (clone, crossover × 2) with:
```cpp
child_bc->append(GeneticOps::copy_instruction(instr));
```

### 2.2 Extract stack drain helper

**File:** `src/evolution/fitness.cpp`

Add private helper:
```cpp
static void drain_stack(etil::core::ExecutionContext& ctx) {
    while (ctx.data_stack().size() > 0) {
        auto v = ctx.data_stack().pop();
        if (v) value_release(*v);
    }
}
```

Replace 3 identical drain loops.

### 2.3 Extract perturb helper

**Files:** `src/evolution/ast_genetic_ops.cpp` and `src/evolution/genetic_ops.cpp`

Add to a shared header (e.g., `include/etil/evolution/mutation_helpers.hpp`):
```cpp
void perturb_numeric_value(etil::core::Instruction::Op op,
                           int64_t& int_val, double& float_val,
                           double stddev, std::mt19937_64& rng);
```

Both `perturb_constant` implementations call this shared function.

### 2.4 Template AST node collector

**File:** `src/evolution/ast_genetic_ops.cpp`

Replace `collect_word_calls_impl` and `collect_literals_impl` with:
```cpp
template<typename Pred>
static void collect_nodes(ASTNode& node, Pred pred, std::vector<ASTNode*>& out) {
    if (pred(node)) out.push_back(&node);
    for (auto& child : node.children) collect_nodes(child, pred, out);
}
```

Usage:
```cpp
collect_nodes(ast, [](const ASTNode& n) { return n.kind == ASTNodeKind::WordCall; }, calls);
collect_nodes(ast, [](const ASTNode& n) {
    return n.kind == ASTNodeKind::Literal &&
           (n.literal_op == Op::PushInt || n.literal_op == Op::PushFloat);
}, literals);
```

### Tests

- All 1353+ tests must pass unchanged (refactoring only, no behavior change)

### Acceptance Criteria

- Net negative lines (more removed than added)
- No duplicate code blocks remaining in evolution module
- All tests pass

---

## Phase 3: Missing Operators

### 3.1 Block move operator

**File:** `src/evolution/ast_genetic_ops.cpp`

New method:
```cpp
bool ASTGeneticOps::move_block(ASTNode& ast);
```

Algorithm:
1. Collect all `WordCall` nodes in the AST
2. Pick a random source node
3. Pick a random target position (different from source)
4. Remove source from its parent `Sequence`'s children
5. Insert at target position
6. Return true (repair will fix type misalignment)

This is simpler than the plan's "functional block" version — it moves single WordCalls, not contiguous subsequences. Subsequence moves can be added later.

### 3.2 Control flow mutation operator

**File:** `src/evolution/ast_genetic_ops.cpp`

New method:
```cpp
bool ASTGeneticOps::mutate_control_flow(ASTNode& ast);
```

Two sub-operations (randomly chosen):

**Wrap in if/then:** Pick a WordCall node, wrap it in an IfThen with a `true` condition (always executes). Evolution can later mutate the condition.
```
Before: WordCall("mat-relu")
After:  IfThen(children=[Sequence([Literal(true), WordCall("mat-relu")])])
```

**Remove zero-effect block:** Find a WordCall whose stack effect is `(0 consumed, 0 produced)` — like `cr` or `drop drop dup dup` — and remove it. This prunes dead code that accumulates through mutation.

### 3.3 Subtree-level crossover

**File:** `src/evolution/ast_genetic_ops.cpp`

Enhance `block_crossover` to operate on `Sequence` children, not just individual WordCalls:

1. For each parent, identify contiguous subsequences of the top-level Sequence with known stack effects (via `StackEffect` annotations)
2. Find pairs with matching `(consumed, produced)` across parents
3. Replace the subsequence in parent A with the matching subsequence from parent B

If no matching subsequences found, fall back to the current WordCall-level swap.

### 3.4 Wire new operators into mutate()

Update `ASTGeneticOps::mutate()` to randomly choose among all 4 operators:
```cpp
std::uniform_int_distribution<int> choice(0, 3);
switch (choice(rng_)) {
    case 0: mutated = substitute_call(ast); break;
    case 1: mutated = perturb_constant(ast); break;
    case 2: mutated = move_block(ast); break;
    case 3: mutated = mutate_control_flow(ast); break;
}
```

### Tests

- `MoveBlockProducesChild` — move a WordCall, verify child has bytecode and executes
- `MoveBlockDifferentOutput` — verify moved block produces different output
- `WrapIfThenProducesChild` — verify wrapped node still executes correctly
- `ControlFlowRemoveZeroEffect` — verify removing a zero-effect word preserves behavior
- `SubtreeCrossover` — verify a multi-word sequence is spliced from parent B

### Acceptance Criteria

- All 4 mutation operators functional
- Crossover handles both WordCall and Sequence levels
- All new operators go through repair before compilation
- All existing tests pass

---

## Phase 4: Missing Tests

### 4.1 Mutation validity rate test

```cpp
TEST_F(ASTGeneticOpsTest, MutationValidityRate) {
    interp.interpret_line(": target 1 2 + 3 * ;");
    ASTGeneticOps ops(dict);

    int valid = 0;
    int total = 100;
    for (int i = 0; i < total; ++i) {
        auto impl = dict.lookup("target");
        auto child = ops.mutate(**impl);
        if (child && child->bytecode()) {
            ExecutionContext ctx(0);
            ctx.set_dictionary(&dict);
            ctx.set_limits(10000, 1000, 100, 1.0);
            bool ok = execute_compiled(*child->bytecode(), ctx);
            // Clean up
            while (ctx.data_stack().size() > 0) {
                auto v = ctx.data_stack().pop();
                if (v) value_release(*v);
            }
            if (ok) valid++;
        }
    }
    double rate = static_cast<double>(valid) / total;
    EXPECT_GT(rate, 0.80);  // >80% validity (plan says >90%, be conservative initially)
}
```

### 4.2 End-to-end activation evolution test

```cpp
TEST_F(ASTGeneticOpsTest, EvolveActivationFunction) {
    // Define a word using mat-relu
    interp.interpret_line(": fwd dup + ;");  // simple word with substitutable calls

    EvolutionConfig config;
    config.generation_size = 5;
    config.population_limit = 10;
    config.use_ast_ops = true;
    EvolutionEngine engine(config, dict);

    // Register test cases that the original word satisfies
    engine.register_tests("fwd",
        {{{Value(int64_t(3))}, {Value(int64_t(6))}},
         {{Value(int64_t(5))}, {Value(int64_t(10))}}});

    // Run 10 generations
    for (int g = 0; g < 10; ++g) {
        engine.evolve_word("fwd");
    }

    // Verify multiple implementations exist
    auto impls = dict.get_implementations("fwd");
    ASSERT_TRUE(impls.has_value());
    EXPECT_GT(impls->size(), 1u);
    EXPECT_EQ(engine.generations_run("fwd"), 10u);
}
```

### 4.3 Specific substitution tests

```cpp
TEST_F(ASTGeneticOpsTest, SubstituteRespectsTagTiers) {
    // Build signature index and verify tiered results
    SignatureIndex index;
    index.rebuild(dict);
    auto results = index.find_tiered(1, 1, {"activation", "element-wise", "shape-preserving"});
    // Level 1 should include mat-sigmoid, mat-tanh (exact tag match with mat-relu)
    bool has_level1 = false;
    for (const auto& [name, level] : results) {
        if (level == 1 && (name == "mat-sigmoid" || name == "mat-tanh")) {
            has_level1 = true;
        }
    }
    EXPECT_TRUE(has_level1);
}
```

### 4.4 Bytecode vs AST mutation validity comparison

```cpp
TEST_F(ASTGeneticOpsTest, ASTBetterThanBytecode) {
    interp.interpret_line(": compare-target 1 2 + 3 * ;");
    auto impl = dict.lookup("compare-target");

    // AST-level mutations
    ASTGeneticOps ast_ops(dict);
    int ast_valid = 0;
    for (int i = 0; i < 50; ++i) {
        auto child = ast_ops.mutate(**impl);
        if (child && child->bytecode()) {
            ExecutionContext ctx(0);
            ctx.set_dictionary(&dict);
            ctx.set_limits(10000, 1000, 100, 1.0);
            if (execute_compiled(*child->bytecode(), ctx)) ast_valid++;
            while (ctx.data_stack().size() > 0) {
                auto v = ctx.data_stack().pop();
                if (v) value_release(*v);
            }
        }
    }

    // Bytecode-level mutations
    GeneticOps bc_ops;
    int bc_valid = 0;
    for (int i = 0; i < 50; ++i) {
        auto child = bc_ops.clone(**impl, dict);
        if (child && child->bytecode()) {
            bc_ops.mutate(*child->bytecode());
            ExecutionContext ctx(0);
            ctx.set_dictionary(&dict);
            ctx.set_limits(10000, 1000, 100, 1.0);
            if (execute_compiled(*child->bytecode(), ctx)) bc_valid++;
            while (ctx.data_stack().size() > 0) {
                auto v = ctx.data_stack().pop();
                if (v) value_release(*v);
            }
        }
    }

    // AST should produce more valid mutations than bytecode
    EXPECT_GT(ast_valid, bc_valid);
}
```

### Acceptance Criteria

- Mutation validity rate test passes at >80%
- End-to-end evolution test creates multiple implementations over 10 generations
- AST validity rate measurably higher than bytecode validity rate
- Tag tier test verifies Level 1 results include expected activation functions

---

## Phase 5: Compile-Time Type Inference

### Goal

Wire the `StackSimulator` into the compiler's `;` handler so every colon definition gets a `TypeSignature` on its `WordImpl` at definition time.

### Changes

**File:** `src/core/compile_handlers.cpp` — in `handle_semicolon()` (or the `finalize_definition_` callback):

After the bytecode is finalized and the `WordImpl` is created:
```cpp
// Infer type signature from bytecode
evolution::Decompiler decompiler;
evolution::StackSimulator simulator;
auto ast = decompiler.decompile(*impl->bytecode());
auto sig = simulator.infer_signature(ast, dict_);
impl->set_signature(sig);
```

This requires the compiler to have access to the `Decompiler` and `StackSimulator`. Since these are stateless classes, they can be instantiated on the stack inside the handler.

**File:** `include/etil/core/word_impl.hpp` — no changes needed (`set_signature` already exists).

### Tests

```cpp
TEST(TypeInferenceTest, ColonDefinitionGetsSignature) {
    interp.interpret_line(": double dup + ;");
    auto impl = dict.lookup("double");
    auto& sig = (*impl)->signature();
    EXPECT_FALSE(sig.inputs.empty());
    EXPECT_FALSE(sig.outputs.empty());
    EXPECT_EQ(sig.inputs.size(), 1u);   // consumes 1
    EXPECT_EQ(sig.outputs.size(), 1u);  // produces 1
}

TEST(TypeInferenceTest, OpaqueWordMarkedVariable) {
    interp.interpret_line(": run-it execute ;");
    auto impl = dict.lookup("run-it");
    auto& sig = (*impl)->signature();
    // execute makes the word opaque
    EXPECT_TRUE(sig.variable_inputs || sig.variable_outputs);
}
```

### Acceptance Criteria

- Every colon definition compiled after this change has a populated `TypeSignature`
- Category 1 words have exact input/output counts
- Category 3 words (containing `execute`/`evaluate`) are flagged with `variable_*`
- All existing tests pass (primitives already have signatures; colon definitions now get them too)
- `SignatureIndex::rebuild()` produces richer results because user-defined words now have signatures

---

## Version Bumping Strategy

| Phase | Bump | Version |
|-------|------|---------|
| 1 | Patch | v1.5.2 |
| 2 | Patch | v1.5.3 |
| 3 | Patch | v1.5.4 |
| 4 | Patch | v1.5.5 |
| 5 | Minor | v1.6.0 |

---

## Dependencies

```
Phase 1: P0 fixes (no deps)
Phase 2: Refactoring (no deps, but cleaner if after Phase 1)
Phase 3: Missing operators (benefits from Phase 2's DRY helpers)
Phase 4: Missing tests (requires Phase 3's operators)
Phase 5: Compile-time inference (independent, can run in parallel with 3-4)
```

Phases 1 and 2 are independent. Phase 3 benefits from Phase 2. Phase 4 requires Phase 3. Phase 5 is independent of 3-4.

---

## Success Criteria (End State)

After all 5 phases:

1. **All P0 issues resolved** — no stale declarations, no logic bugs, no DoS vectors
2. **Zero code duplication** in evolution module — all shared patterns extracted
3. **5 mutation operators functional** — substitute, perturb, move, control flow, crossover (WordCall + subtree)
4. **Mutation validity rate >80%** — measured and tested
5. **AST > bytecode** — measured and tested (AST produces more valid mutations)
6. **End-to-end evolution works** — word evolves over 10 generations with multiple surviving implementations
7. **Compile-time type inference** — every colon definition gets a TypeSignature
8. **Stage 6 score: 10/10**
