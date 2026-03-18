# Implementation Plan: MLP Layers 1–3

**Date:** 2026-03-18
**Reference:** `20260318-MLP-Status-Report.md`, `20260310-ETIL-MLP-Design-Analysis.md`
**Current Version:** ETIL v1.0.0
**Scope:** Layers 1–3. Layer 4 (evolutionary neural architecture search) is deferred.

---

## Overview

| Layer | What | Language | Effort | Depends On |
|-------|------|----------|--------|------------|
| 1 | TIL-level MLP library | TIL only | ~200 lines TIL | Nothing (all primitives exist) |
| 2 | Selection engine | C++ | ~200–300 lines | Nothing (wires into existing `lookup()`) |
| 3 | Evolution engine | C++ | ~500–700 lines | Layer 2 (needs selection to be useful) |

Layer 1 is independent of Layers 2–3. Layer 3 depends on Layer 2. All three layers can proceed without modifying any existing primitives.

---

## Layer 1: TIL-Level MLP Library

### Goal

Write a reusable MLP library in pure TIL (`data/mlp.til`) that can define, train, and evaluate feedforward neural networks. Validate with XOR (toy) and a small classification task.

### Prerequisite: `mat-col-vec`

Before building the MLP words, add one small C++ primitive:

```
mat-col-vec  ( mat j -- mat )   \ Extract column j as an (n x 1) HeapMatrix
```

This avoids the `mat-col` → `array->mat` workaround that produces wrong-shaped matrices. ~15 lines in `matrix_primitives.cpp` following the `mat-col` pattern but returning a HeapMatrix instead of a HeapArray.

### 1.1 Data Representation

A **layer** is a `HeapMap` with three keys:

| Key | Value | Purpose |
|-----|-------|---------|
| `"W"` | HeapMatrix (out x in) | Weight matrix |
| `"b"` | HeapMatrix (out x 1) | Bias column vector |
| `"act"` | Xt (execution token) | Activation function |

A **network** is a `HeapArray` of layer maps.

A **training cache** is a `HeapArray` of `HeapMap`s (one per layer), each storing:

| Key | Value | Purpose |
|-----|-------|---------|
| `"Z"` | HeapMatrix | Pre-activation (W*A_prev + b) |
| `"A"` | HeapMatrix | Post-activation (act(Z)) |

### 1.2 Words to Implement

**Network construction:**

```forth
: make-layer  ( fan_in fan_out act-xt -- layer-map )
    \ Creates a layer map with Xavier-initialized weights and zero bias

: make-mlp  ( layer1 layer2 ... n -- network )
    \ Collects n layers from the stack into an array
```

**Forward pass:**

```forth
: layer-forward  ( A_prev layer -- A )
    \ Single layer: A = act(W * A_prev + b)

: forward  ( X network -- Y )
    \ Iterate layers, return final output

: forward-cache  ( X network -- Y cache )
    \ Forward pass that caches Z and A per layer (needed for backprop)
```

**Backpropagation:**

```forth
: layer-backward  ( dA Z A_prev W act'-xt m -- dA_prev dW db )
    \ Single layer backward pass:
    \   dZ = dA hadamard act'(Z)
    \   dW = (1/m) * dZ * A_prev^T
    \   db = (1/m) * col-sum(dZ)
    \   dA_prev = W^T * dZ

: backward  ( Y_hat Y network cache -- grads )
    \ Full backward pass, returns array of {dW, db} maps
```

**Parameter update:**

```forth
: sgd-update  ( network grads lr -- network )
    \ W' = W - lr * dW, b' = b - lr * db for each layer
```

**Training loop:**

```forth
: train-epoch  ( X Y network lr -- network loss )
    \ One full forward + backward + update cycle
    \ Returns updated network and loss value

: train  ( X Y network lr epochs -- network )
    \ Run train-epoch in a loop, print loss every N epochs
```

**Activation derivative dispatch:**

The backward pass needs the derivative corresponding to each layer's activation. Since activation functions are stored as execution tokens, we need a way to look up the derivative. Two options:

- **Option A:** Store `"act'"` as a fourth key in the layer map (explicit, simple).
- **Option B:** A dispatch word that maps `['] mat-relu` → `['] mat-relu'` etc. (clever, fragile).

**Recommendation:** Option A. Add `"act'"` to `make-layer`. Simple is better.

### 1.3 File Organization

| File | Purpose |
|------|---------|
| `data/mlp.til` | Library words (loaded via `include`) |
| `examples/tui/xor-mlp.til` | XOR training demo |
| `tests/til/test_mlp.til` | Integration tests |
| `tests/til/test_mlp.sh` | Test launcher |

### 1.4 Validation

**XOR network** — 2 inputs, 4 hidden (ReLU), 1 output (sigmoid). Should converge to <0.01 MSE loss within 5,000 epochs. This is the canonical "does your backprop work?" test.

**Expected output:**
```
Epoch 0: loss = 0.2500
Epoch 1000: loss = 0.0412
Epoch 2000: loss = 0.0038
Epoch 3000: loss = 0.0009
Training complete.
0 0 -> 0.02
0 1 -> 0.97
1 0 -> 0.98
1 1 -> 0.01
```

### 1.5 Risks and Mitigations

**Allocation pressure:** Every matrix op allocates. A 4-layer network with batch size 1 does ~40 matrix allocations per training step. At 5,000 epochs, that's 200,000 allocations. With `HeapMatrix` being ~64 bytes + data, this should be fine for toy problems. In-place variants (`mat+!` etc.) are a future optimization.

**Stack depth:** Backprop through 4 layers with intermediate values on the stack/return stack could get deep. Use `HeapMap` caches (allocated on heap) rather than return-stack gymnastics.

**Numerical stability:** `mat-sigmoid` and `mat-softmax` already handle numerical stability in C++. The TIL layer doesn't need to worry about this.

---

## Layer 2: Selection Engine

### Goal

Implement a `SelectionEngine` that replaces the deterministic "last registered wins" `lookup()` behavior with weighted probabilistic selection among multiple implementations of a word. When a word has one implementation (the common case), performance is identical. When a word has multiple implementations, selection is based on `weight_` values.

### 2.1 Design

**New class:** `SelectionEngine` in `include/etil/core/selection_engine.hpp` / `src/selection/selection_engine.cpp`.

```cpp
namespace etil::selection {

enum class Strategy {
    Latest,          // Current behavior: always pick .back()
    WeightedRandom,  // Probability proportional to weight_
    EpsilonGreedy,   // Exploit best (1-ε), explore random (ε)
    UCB1             // Upper Confidence Bound (exploration bonus)
};

class SelectionEngine {
public:
    explicit SelectionEngine(Strategy strategy = Strategy::Latest);

    // Select one implementation from a concept's implementations
    // Returns nullptr if impls is empty
    WordImpl* select(const std::vector<WordImplPtr>& impls);

    // Configuration
    void set_strategy(Strategy s);
    Strategy strategy() const;
    void set_epsilon(double eps);  // For EpsilonGreedy

private:
    Strategy strategy_;
    double epsilon_ = 0.1;
    // Per-thread RNG (no locking needed)
    static thread_local std::mt19937_64 rng_;
};

} // namespace etil::selection
```

### 2.2 Selection Strategies

**`Latest`** — Returns `impls.back()`. Identical to current behavior. This is the default, so nothing changes unless explicitly switched.

**`WeightedRandom`** — Normalize weights to probabilities, sample. All `weight_` values are ≥0 (enforced by `set_weight`). If all weights are equal, this is uniform random. If one weight dominates, it's nearly deterministic.

```cpp
WordImpl* select_weighted(const std::vector<WordImplPtr>& impls) {
    double total = 0.0;
    for (auto& impl : impls) total += impl->weight();
    if (total <= 0.0) return impls.back().get();  // fallback

    std::uniform_real_distribution<double> dist(0.0, total);
    double r = dist(rng_);
    double cumulative = 0.0;
    for (auto& impl : impls) {
        cumulative += impl->weight();
        if (r < cumulative) return impl.get();
    }
    return impls.back().get();  // rounding guard
}
```

**`EpsilonGreedy`** — With probability (1-ε), pick the highest-weight implementation. With probability ε, pick uniformly at random. Classic explore/exploit tradeoff.

**`UCB1`** — Upper Confidence Bound. Balances exploitation (high success rate) with exploration (under-tested implementations). Score = success_rate + C * sqrt(ln(total_calls) / impl_calls). Pick the highest-scoring impl. This is the most sophisticated strategy and naturally drives exploration toward under-tested alternatives.

### 2.3 Wiring Into the Execution Path

Two call sites need modification:

**1. `Interpreter::interpret_token()` (line ~388 in `interpreter.cpp`)**

Current:
```cpp
auto lookup = dict_->lookup(token);
```

New:
```cpp
auto lookup = selection_engine_
    ? dict_->select(token, *selection_engine_)
    : dict_->lookup(token);
```

**2. `execute_compiled()` `Op::Call` (line ~50 in `compiled_body.cpp`)**

Current:
```cpp
auto result = dict->lookup(instr.word_name);
```

New:
```cpp
auto result = ctx.selection_engine()
    ? dict->select(instr.word_name, *ctx.selection_engine())
    : dict->lookup(instr.word_name);
```

**Note on caching:** The `Op::Call` instruction currently caches the looked-up impl to avoid repeated dictionary lookups. With selection enabled, the cache must be bypassed — each execution should potentially select a different implementation. The cache should only be used when `selection_engine()` is null (i.e., `Strategy::Latest` or no engine).

### 2.4 Dictionary API Addition

```cpp
// In dictionary.hpp:
std::optional<WordImplPtr> select(
    const std::string& word,
    selection::SelectionEngine& engine
) const;
```

Implementation: lock, find concept, call `engine.select(concept.implementations)`, return result. Same locking as `lookup()`.

### 2.5 ExecutionContext / Interpreter Extensions

```cpp
// ExecutionContext gains:
void set_selection_engine(selection::SelectionEngine* engine);
selection::SelectionEngine* selection_engine() const;

// Interpreter gains:
void set_selection_engine(selection::SelectionEngine* engine);
```

The `SelectionEngine` is owned externally (by `Session` in MCP mode, or by the REPL main). `ExecutionContext` and `Interpreter` hold non-owning pointers (same pattern as `Dictionary*`, `Lvfs*`, `UvSession*`).

### 2.6 TIL-Level Control

Three new primitives for runtime strategy switching:

```
select-strategy  ( strategy-int -- )   \ 0=latest, 1=weighted, 2=epsilon, 3=ucb1
select-epsilon   ( float -- )          \ Set epsilon for epsilon-greedy
select-off       ( -- )                \ Revert to Latest (deterministic)
```

### 2.7 File Organization

| File | Purpose |
|------|---------|
| `include/etil/selection/selection_engine.hpp` | Class definition |
| `src/selection/selection_engine.cpp` | Strategy implementations |
| `tests/unit/test_selection_engine.cpp` | Unit tests |

### 2.8 Testing

- **Unit tests (~20):** Each strategy with known weights, edge cases (single impl, zero weights, all equal weights), epsilon=0 and epsilon=1 boundaries, UCB1 with varying call counts.
- **Integration test:** Define a word with 3 implementations at different weights, run it 1000 times, verify the distribution roughly matches the weights (chi-squared or simple ratio check).
- **Regression:** All 1252 existing tests must pass unchanged (default strategy is `Latest`, which is identical to current `lookup().back()`).

### 2.9 MCP Integration

- New MCP tool: `set_selection_strategy` — set strategy and epsilon for the current session.
- Existing `set_weight` tool: already works, now actually affects execution when strategy != Latest.
- `get_word_info` response: already includes weight and profile data per implementation.

---

## Layer 3: Evolution Engine

### Goal

Implement a `GeneticOps` module and an `EvolutionEngine` that can clone, mutate, and crossover `WordImpl` bytecode, evaluate fitness against test cases, and update implementation weights based on results.

### 3.1 Scope Boundaries

**In scope:**
- Bytecode-level mutation (swap instructions, change constants, insert/delete instructions)
- Bytecode-level crossover (combine subsequences from two parents)
- Fitness evaluation (run implementation against test cases, measure correctness + speed)
- Weight updates based on fitness (connect to Layer 2's selection engine)
- Population management (add new implementations, prune worst performers)

**Out of scope (deferred to Layer 4):**
- Neural architecture search (evolving MLP topologies)
- Evolving activation function selection
- Evolving learning rate schedules
- Multi-objective optimization
- Distributed evolution

### 3.2 GeneticOps

**New class:** `GeneticOps` in `include/etil/evolution/genetic_ops.hpp` / `src/evolution/genetic_ops.cpp`.

```cpp
namespace etil::evolution {

struct MutationConfig {
    double instruction_swap_prob = 0.3;
    double constant_perturb_prob = 0.3;
    double instruction_insert_prob = 0.1;
    double instruction_delete_prob = 0.1;
    double constant_perturb_stddev = 0.1;
    size_t max_bytecode_length = 256;
};

class GeneticOps {
public:
    explicit GeneticOps(MutationConfig config = {});

    // Clone a WordImpl: copies bytecode, increments generation,
    // records parent ID
    WordImplPtr clone(const WordImpl& parent, Dictionary& dict);

    // Mutate a bytecode sequence in-place
    // Returns true if any mutation was applied
    bool mutate(ByteCode& code);

    // Crossover: create child from two parents' bytecode
    // Single-point crossover at a random instruction boundary
    WordImplPtr crossover(
        const WordImpl& parent_a,
        const WordImpl& parent_b,
        Dictionary& dict
    );

private:
    MutationConfig config_;
    std::mt19937_64 rng_;

    // Mutation operators
    void swap_instructions(ByteCode& code);
    void perturb_constant(ByteCode& code);
    void insert_instruction(ByteCode& code);
    void delete_instruction(ByteCode& code);
};

} // namespace etil::evolution
```

#### Mutation Operators

**Instruction swap:** Pick two random instruction indices, swap them. Only valid for instructions that don't break control flow (skip `Branch`, `BranchIfFalse`, `DoSetup`, `DoLoop`). This is the safest mutation — it can't create invalid bytecode, only semantically different programs.

**Constant perturbation:** Find a `PushInt` or `PushFloat` instruction, add Gaussian noise to its value. For integers: `val += round(normal(0, stddev * abs(val)))`. For floats: `val += normal(0, stddev * abs(val))`. This is the most useful mutation for numerical optimization.

**Instruction insertion:** Insert a copy of a random existing instruction at a random position. Bounded by `max_bytecode_length`. Can create redundant but potentially useful code.

**Instruction deletion:** Remove a random non-control-flow instruction. Only if bytecode length > 2 (preserve minimum viability).

### 3.3 Fitness Evaluation

**New class:** `Fitness` in `include/etil/evolution/fitness.hpp` / `src/evolution/fitness.cpp`.

```cpp
namespace etil::evolution {

struct TestCase {
    std::vector<Value> inputs;    // Push these before execution
    std::vector<Value> expected;  // Compare stack after execution
};

struct FitnessResult {
    double correctness;  // 0.0–1.0 (fraction of test cases passed)
    double speed;        // Mean execution time in nanoseconds
    double fitness;      // Combined score (configurable weighting)
    size_t tests_passed;
    size_t tests_total;
};

class Fitness {
public:
    // Evaluate an implementation against test cases
    // Creates a temporary ExecutionContext, runs impl, checks results
    FitnessResult evaluate(
        WordImpl& impl,
        const std::vector<TestCase>& tests,
        Dictionary& dict,
        size_t instruction_budget = 100000
    );

    // Weight for combining correctness and speed
    void set_speed_weight(double w);  // default 0.1

private:
    double speed_weight_ = 0.1;
};

} // namespace etil::evolution
```

Fitness evaluation runs each test case in an isolated `ExecutionContext` with a tight instruction budget. A crashed or timed-out implementation gets correctness=0 for that test case. Speed is only meaningful for correct implementations.

### 3.4 EvolutionEngine

**New class:** `EvolutionEngine` in `include/etil/evolution/evolution_engine.hpp` / `src/evolution/evolution_engine.cpp`.

```cpp
namespace etil::evolution {

struct EvolutionConfig {
    size_t population_limit = 10;     // Max impls per concept
    size_t generation_size = 5;       // Children per generation
    double mutation_rate = 0.8;       // Prob of mutation vs crossover
    double prune_threshold = 0.01;    // Remove impls with weight below this
    MutationConfig mutation_config;
};

class EvolutionEngine {
public:
    EvolutionEngine(EvolutionConfig config, Dictionary& dict);

    // Run one generation of evolution for a word concept
    // 1. Select parents (weighted by fitness)
    // 2. Create children (mutate or crossover)
    // 3. Evaluate children against test cases
    // 4. Update weights based on fitness
    // 5. Prune worst performers if over population_limit
    void evolve_word(
        const std::string& word,
        const std::vector<TestCase>& tests
    );

    // Evolve all words that have test cases registered
    void evolve_all();

    // Register test cases for a word
    void register_tests(
        const std::string& word,
        std::vector<TestCase> tests
    );

    // Query
    const EvolutionConfig& config() const;
    size_t generations_run(const std::string& word) const;

private:
    EvolutionConfig config_;
    Dictionary& dict_;
    GeneticOps genetic_ops_;
    Fitness fitness_;
    SelectionEngine parent_selector_;

    // Per-word state
    struct WordEvolution {
        std::vector<TestCase> tests;
        size_t generations = 0;
    };
    std::unordered_map<std::string, WordEvolution> word_state_;

    // Weight update: normalize fitness scores to weights
    void update_weights(
        const std::string& word,
        const std::vector<std::pair<WordImplPtr, FitnessResult>>& results
    );

    // Remove implementations below prune_threshold
    void prune(const std::string& word);
};

} // namespace etil::evolution
```

### 3.5 TIL-Level Interface

New primitives for driving evolution from TIL code:

```
evolve-register  ( word-str tests-array -- flag )
    \ Register test cases for a word. Each test is a map:
    \ { "in": [values...], "out": [values...] }

evolve-word  ( word-str -- flag )
    \ Run one generation of evolution

evolve-all  ( -- )
    \ Evolve all registered words

evolve-status  ( word-str -- json )
    \ Return evolution state: generation count, population size,
    \ fitness scores per implementation
```

### 3.6 Dictionary Extension

The `Dictionary` needs one new method for evolution:

```cpp
// Remove the worst implementation of a word (by weight)
// Used by EvolutionEngine::prune()
bool remove_weakest(const std::string& word, double threshold);
```

### 3.7 File Organization

| File | Purpose |
|------|---------|
| `include/etil/evolution/genetic_ops.hpp` | Mutation and crossover |
| `include/etil/evolution/fitness.hpp` | Fitness evaluation |
| `include/etil/evolution/evolution_engine.hpp` | Evolution loop |
| `src/evolution/genetic_ops.cpp` | Mutation operators |
| `src/evolution/fitness.cpp` | Test case evaluation |
| `src/evolution/evolution_engine.cpp` | Generation loop, weight updates, pruning |
| `tests/unit/test_genetic_ops.cpp` | Mutation/crossover unit tests |
| `tests/unit/test_fitness.cpp` | Fitness evaluation tests |
| `tests/unit/test_evolution_engine.cpp` | Full evolution loop tests |

### 3.8 Testing

- **GeneticOps (~15 tests):** Clone preserves semantics, each mutation operator produces valid bytecode, crossover produces valid bytecode, mutation respects max length, control-flow instructions are protected.
- **Fitness (~10 tests):** Correct implementation scores 1.0, wrong implementation scores 0.0, crashed implementation scores 0.0, budget-exceeded implementation scores 0.0, speed measurement is positive.
- **EvolutionEngine (~15 tests):** Population grows to limit, prune removes weak impls, weights shift toward better fitness, generations counter increments, test registration works, evolve with no tests is a no-op.

### 3.9 Safety Constraints

- **Bytecode validation:** After any mutation, validate that control flow is balanced (branches have valid targets, DO/LOOP pairs match). Discard invalid mutations rather than crashing.
- **Instruction budget:** All fitness evaluations run with a tight budget (default 100K instructions). A mutant that loops forever is killed, not waited on.
- **Population cap:** `population_limit` prevents unbounded growth of implementations per concept.
- **Prune threshold:** Implementations with weight < 0.01 are removed to prevent dead weight accumulation.
- **Thread safety:** `EvolutionEngine` is single-threaded (called from the interpreter thread). No additional synchronization needed beyond the existing `Dictionary` mutex.

---

## Implementation Order

```
Phase A: mat-col-vec primitive (prerequisite)
         ~15 lines C++, 1 version bump

Phase B: Layer 1 — TIL MLP library
         ~200 lines TIL, XOR validation
         No version bump (TIL-only, loaded via include)

Phase C: Layer 2 — Selection engine
         ~200-300 lines C++, wire into lookup path
         1 version bump

Phase D: Layer 3 — Evolution engine
         ~500-700 lines C++
         1 version bump

Phase E: Integration demo
         Evolve a simple word (e.g., evolve activation function
         choice for the XOR MLP from Layer 1)
         Validates all three layers working together
```

Phases A and B can run in the same session. Phase C is independent of A/B. Phase D requires Phase C. Phase E requires all prior phases.

---

## Success Criteria

**Layer 1:** XOR network converges to <0.01 MSE loss. All operations use existing primitives. No C++ changes beyond `mat-col-vec`.

**Layer 2:** All 1252 existing tests pass with default `Latest` strategy. A word with 3 implementations at weights 0.7/0.2/0.1 selects proportionally when strategy is `WeightedRandom`. Strategy can be changed at runtime via TIL primitives.

**Layer 3:** Given a word `: double dup + ;` and test cases `{in: [3], out: [6]}, {in: [5], out: [10]}`, the evolution engine can clone, mutate, evaluate, and update weights. After N generations, the highest-weight implementation still passes all test cases.

**Integration:** An MLP trains on XOR with evolved activation function selection, demonstrating the full stack from TIL-level neural network training through the selection engine to the evolution engine.
