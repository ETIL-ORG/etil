# Distance-Based Fitness Functions for AST Evolution

**Date:** 2026-03-25
**References:** `20260324-Evolution-Experiments-Report.md`, `20260324-AST-Evolution-Grow-Shrink-Design.md`
**Prerequisites:** v1.7.5 (logging, tags, grow/shrink, pools)
**Status:** Design

---

## Problem Statement

The AST evolution engine's mutation operators (substitute, grow, shrink, perturb, move, control flow) are now working correctly. Pool-restricted substitution produces only domain-relevant candidates. Grow/shrink change program length. Semantic tags guide substitution toward related words. The logging system provides full visibility into every mutation decision.

**But the engine cannot converge on the test problems.**

After 500 generations with a math-only pool, the symbolic regression experiment (`f(x) = x² + 3x + 5`) achieves **0/7 test passes**. The function synthesis experiment (`f(x,y) = x*y + x - y`) achieves **2/5 test passes** — progress, but stuck. The root cause is not the mutation operators. It's the fitness function.

---

## The Binary Fitness Problem

The current fitness function (`fitness.cpp`) uses **exact-match scoring**:

```cpp
// For each test case, the actual output must EXACTLY match the expected output.
// Integers: actual->as_int != exp.as_int → fail
// Floats: |actual - expected| > 1e-9 → fail
// Result: correctness = tests_passed / tests_total (0.0 to 1.0)
```

This is a **cliff edge** fitness landscape:

```
Fitness
  1.0 ┤                          ╭──── correct
      │                          │
  0.0 ┤─────────────────────────╯ everything else
      └─────────────────────────────────
              Program space →
```

Every program that doesn't produce the exact answer gets `correctness = 0.0`. There is no gradient. A program that outputs 4 when the answer is 5 has the same fitness as one that outputs 1000000. The engine has no signal to guide it toward better programs.

### Evidence from the Experiments

**Symbolic regression** (500 generations, 2500 children evaluated):

| Metric | Value |
|---|---|
| Children with 0/7 passes | 2500 (100%) |
| Children with ≥1/7 passes | 0 (0%) |
| Best fitness | 0.10 (prune threshold, i.e., zero correctness) |

The target is `f(x) = x² + 3x + 5`. A program producing `f(x) = x + 5` would pass 0/7 tests (no exact match) but is clearly *closer* than `f(x) = mat-inv(x)`. The binary fitness treats them identically.

**Function synthesis** (200 generations, 1000 children):

| Metric | Value |
|---|---|
| Children with 0/5 passes | 47 (5%) |
| Children with 1/5 passes | 462 (46%) |
| Children with 2/5 passes | 491 (49%) |
| Children with ≥3/5 passes | 0 (0%) |

This experiment has a partial gradient because some test cases happen to match intermediate programs. The engine evolved from `+` toward `-` (which matches 2 test cases: `f(0,5)=-5` and `f(5,0)=5`). But it's stuck at 2/5 because the next improvement requires `x*y + x - y` — a structural leap that binary fitness can't guide.

**Sorting network** (1000 generations, manual fitness):

The sorting network experiment succeeded because its fitness function **IS distance-based**: `score = sorted_count × 100 - comparator_count`. A network that sorts 18/24 permutations scores higher than one that sorts 12/24. This smooth gradient guided the (1+1) ES to convergence in 116 generations.

---

## Design: Distance-Based Fitness

### Core Concept

Replace exact-match comparison with **per-test-case distance scoring**. For each test case, compute how far the actual output is from the expected output. Closer = higher fitness.

```
Per-test score = 1.0 / (1.0 + distance(actual, expected))
```

Where `distance()` is type-appropriate:
- **Integer/Float**: `|actual - expected|` (absolute difference)
- **Boolean**: 0 if equal, 1 if different
- **String**: edit distance (Levenshtein) or 0/1 exact match
- **Array/Matrix**: element-wise distance, normalized by length
- **Type mismatch**: maximum distance (penalty)
- **Stack depth wrong**: penalty proportional to depth difference
- **Execution failed**: maximum penalty (0.0 score)

### Score Formula

For a test suite of N test cases:

```
correctness = (1/N) × Σ per_test_score_i

per_test_score_i = 1.0 / (1.0 + α × distance_i)
```

Where `α` is a scaling factor (default 1.0) that controls the gradient steepness. With `α = 1.0`:
- distance 0 → score 1.0 (exact match)
- distance 1 → score 0.5
- distance 10 → score 0.091
- distance 100 → score 0.0099

This produces a smooth gradient that rewards programs producing outputs closer to the target, even when no test case is an exact match.

### Comparison: Binary vs Distance

For `f(x) = x² + 3x + 5` with input x=2, expected output 15:

| Program output | Binary score | Distance score |
|---|---|---|
| 15 (exact) | 1.0 | 1.0 |
| 14 | 0.0 | 0.50 |
| 12 | 0.0 | 0.25 |
| 4 (= 2+2, the seed) | 0.0 | 0.083 |
| 0 | 0.0 | 0.063 |
| 1000 | 0.0 | 0.001 |
| execution failed | 0.0 | 0.0 |

With distance-based fitness, `f(x)=14` is visibly better than `f(x)=4`, which is better than `f(x)=1000`. The engine has a gradient to climb.

### Use Cases

**1. Symbolic regression** — Evolve a polynomial from test cases. Distance between integer outputs drives the search toward programs that compute values in the right range, even when not exact.

**2. Function synthesis** — Evolve a multi-input function. Distance scoring converts the 0% → 100% cliff into a smooth ramp. A program that's off by 1 on each test case scores much higher than one that's off by 100.

**3. Constant discovery** — The perturb mutation adjusts numeric constants with Gaussian noise. With binary fitness, a constant that's close-but-not-exact gets zero credit. With distance fitness, the perturb mutation can hill-climb toward the exact constant value.

**4. Neural network architecture search** — Evolve MLP topologies. Distance-based fitness on training loss lets the engine compare architectures even when none achieve zero loss.

**5. String transformation** — Evolve a string processing function. Edit distance between actual and expected strings provides a gradient toward the correct transformation.

---

## Distance Functions by Type

### Numeric Distance (Integer and Float)

```cpp
double numeric_distance(const Value& actual, const Value& expected) {
    double a = (actual.type == Value::Type::Float) ? actual.as_float
             : static_cast<double>(actual.as_int);
    double e = (expected.type == Value::Type::Float) ? expected.as_float
             : static_cast<double>(expected.as_int);
    return std::abs(a - e);
}
```

Cross-type comparison: Integer 5 vs Float 5.0 → distance 0.0. This is intentional — the evolution engine should not penalize type differences when the numeric value is correct.

### Boolean Distance

```cpp
double boolean_distance(const Value& actual, const Value& expected) {
    return (actual.as_int == expected.as_int) ? 0.0 : 1.0;
}
```

### String Distance

For short strings (≤100 chars), use Levenshtein edit distance. For longer strings, fall back to exact match (0 or string_length) to avoid O(n²) cost.

```cpp
double string_distance(const HeapString* actual, const HeapString* expected) {
    if (actual->view() == expected->view()) return 0.0;
    if (actual->length() > 100 || expected->length() > 100) {
        return static_cast<double>(std::max(actual->length(), expected->length()));
    }
    return static_cast<double>(levenshtein(actual->view(), expected->view()));
}
```

### Array Distance

Element-wise numeric distance, normalized by expected length. Missing or extra elements contribute a penalty per element.

```cpp
double array_distance(const HeapArray* actual, const HeapArray* expected) {
    size_t max_len = std::max(actual->length(), expected->length());
    if (max_len == 0) return 0.0;
    double total = 0.0;
    for (size_t i = 0; i < max_len; ++i) {
        if (i >= actual->length() || i >= expected->length()) {
            total += 1.0;  // missing/extra element penalty
        } else {
            Value a, e;
            actual->get(i, a);
            expected->get(i, e);
            total += value_distance(a, e);  // recursive
            value_release(a);
            value_release(e);
        }
    }
    return total / static_cast<double>(max_len);
}
```

### Type Mismatch

When actual and expected have different types, the distance is a fixed penalty. This is larger than any reasonable numeric distance but smaller than execution failure.

```cpp
constexpr double TYPE_MISMATCH_DISTANCE = 1000.0;
```

### Stack Depth Mismatch

When the program produces a different number of outputs than expected:

```cpp
double stack_depth_penalty = abs(actual_depth - expected_depth) * 100.0;
```

This is added to the total distance, not per-element. A program that produces 0 outputs when 1 is expected gets a 100.0 penalty, which scores 0.0099 — much better than execution failure (0.0) but much worse than any output in the right ballpark.

### Execution Failure

Programs that crash, exceed the instruction budget, or produce stack underflow get distance = infinity → score 0.0. Same as the current binary behavior.

---

## Configuration

### Fitness Mode

Two modes, selectable per-word (or globally):

```cpp
enum class FitnessMode {
    Binary,    // Current behavior: exact match (0 or 1 per test)
    Distance,  // Distance-based scoring (0.0 to 1.0 per test)
};
```

Default: `Distance` for new registrations, `Binary` for backward compatibility with existing test suites.

### Scaling Factor

The `α` parameter in `1.0 / (1.0 + α × distance)` controls gradient steepness:
- `α = 0.1`: gentle gradient, large distances still get meaningful scores
- `α = 1.0`: moderate (default)
- `α = 10.0`: steep, only very close outputs score well

Configurable per-word via `evolve-register` or globally on `EvolutionConfig`.

### TIL Interface

```
evolve-fitness-mode  ( n -- )      # 0=binary, 1=distance
evolve-fitness-alpha ( f -- )      # scaling factor (default 1.0)
```

Or bundled into `evolve-register-pool`:

```
evolve-register-pool ( word tests pool -- flag )   # existing, uses config default
```

---

## Implementation Outline

### Modified Files

| File | Change |
|---|---|
| `include/etil/evolution/fitness.hpp` | Add `FitnessMode`, `distance_alpha` to config. Add `value_distance()`, `run_single_test_distance()`. Extend `FitnessResult` with `mean_distance`. |
| `src/evolution/fitness.cpp` | Implement distance functions. Add distance-mode evaluation path. |
| `include/etil/evolution/evolution_engine.hpp` | Add `FitnessMode` and `distance_alpha` to `EvolutionConfig`. Add `fitness_mode` to `WordEvolution`. |
| `src/evolution/evolve_logger.cpp` | Log distance scores at granular level. |

### FitnessResult Extension

```cpp
struct FitnessResult {
    double correctness = 0.0;    // 0.0-1.0 (binary: fraction passed; distance: mean score)
    double speed = 0.0;          // Mean execution time in nanoseconds
    double fitness = 0.0;        // Combined score
    size_t tests_passed = 0;     // Exact matches (even in distance mode)
    size_t tests_total = 0;
    double mean_distance = 0.0;  // NEW: average distance across all tests (lower = better)
};
```

### Backward Compatibility

Binary mode is preserved as the default. Existing `evolve-register` calls use `FitnessMode::Binary`. Only `evolve-register-pool` or explicit `evolve-fitness-mode` activates distance scoring. All existing tests pass unchanged.

---

## Expected Impact on Experiments

### Symbolic Regression

With distance fitness, the initial program `dup +` (= 2x) produces:

| Input x | Expected | Actual (2x) | Distance | Score |
|---|---|---|---|---|
| 0 | 5 | 0 | 5.0 | 0.167 |
| 1 | 9 | 2 | 7.0 | 0.125 |
| 2 | 15 | 4 | 11.0 | 0.083 |
| 3 | 26 | 6 | 20.0 | 0.048 |
| 5 | 45 | 10 | 35.0 | 0.028 |
| -1 | 3 | -2 | 5.0 | 0.167 |
| 10 | 135 | 20 | 115.0 | 0.009 |

Mean score: 0.089. This is nonzero — the engine has something to improve.

A mutation producing `dup * dup +` (= x² + x) would score:

| Input x | Expected | Actual (x²+x) | Distance | Score |
|---|---|---|---|---|
| 0 | 5 | 0 | 5.0 | 0.167 |
| 1 | 9 | 2 | 7.0 | 0.125 |
| 2 | 15 | 6 | 9.0 | 0.100 |
| 3 | 26 | 12 | 14.0 | 0.067 |
| 5 | 45 | 30 | 15.0 | 0.063 |
| -1 | 3 | 0 | 3.0 | 0.250 |
| 10 | 135 | 110 | 25.0 | 0.038 |

Mean score: 0.116. **Higher than the seed's 0.089**. The engine would select this variant over the original. Then perturb/grow can refine the constants toward 3 and 5.

### Function Synthesis

The 2/5 plateau would become a slope. Programs computing `x - y` (distance ~6 on average) would score lower than programs computing `x*y - y` (distance ~4 on average), which would score lower than `x*y + x - y` (distance 0, exact).

### Sorting Network

Already uses distance-based fitness (sorted_count). No change needed.

---

## Risks

**Low risk**: Distance scoring never produces a lower score than binary scoring for an exact match (distance 0 → score 1.0). Correct programs are still maximally fit.

**Medium risk**: Distance scaling may be too gentle or too steep for specific problems. The `alpha` parameter controls this, but the right value is problem-dependent. Mitigation: default alpha=1.0 works for most numeric problems; provide TIL-level control for tuning.

**Low risk**: String and array distance functions add O(n) or O(n²) computation per test case. Mitigated by the 100-char Levenshtein cutoff and the existing instruction budget.

**No risk to binary mode**: Binary evaluation is preserved as a separate code path. Existing tests and behavior are unchanged.
