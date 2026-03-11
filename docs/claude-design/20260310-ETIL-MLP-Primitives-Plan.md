# ETIL MLP Primitives — Implementation Plan

## Context

The design document `docs/claude-design/20260310-ETIL-MLP-Design-Analysis.md` identifies critical gaps in ETIL's matrix subsystem that prevent building a Multilayer Perceptron. The existing `mat*` (DGEMM), `mat+`, `mat-`, `mat-scale`, `mat-transpose`, `mat-rand` provide the foundation, but element-wise activation functions, broadcasting, normal-distribution initialization, and reduction operations are missing. Without these, every matrix element must be processed through the interpreter individually — a ~1000x performance cliff.

This plan adds **17 new C++ primitives** and **1 scalar math word** across 3 stages, enabling forward pass, backpropagation, and classification in pure TIL.

## Key Design Decisions

1. **All new words go in `matrix_primitives.cpp`** — file is 838 lines, grows to ~1100. Same helpers, same `ETIL_LINALG_ENABLED` gate, same registration function. No new files, no CMake changes.

2. **Use `prng_engine()` for `mat-randn`** — already exists in `primitives.cpp` (line 1050), seeded by `random-seed`. Expose it in `primitives.hpp` so `mat-randn` uses the same PRNG. Also refactor `mat-rand` to use it (makes `random-seed` control all randomness for reproducible ML experiments).

3. **Derivative words take pre-activation input** — `mat-relu'`, `mat-sigmoid'`, `mat-tanh'` compute f'(x) from x, not from f(x). This matches the backprop pattern: cache pre-activation Z, compute f'(Z).

4. **One version bump per stage** — each stage is a self-contained, deployable deliverable.

---

## Stage 1: Forward Pass Primitives

**Version bump**: v0.8.19 (current: v0.8.18)

### New Words (6)

| Word | Stack Effect | C++ Function | Implementation |
|------|-------------|--------------|----------------|
| `mat-relu` | `( mat -- mat )` | `prim_mat_relu` | `std::max(0.0, x)` per element |
| `mat-sigmoid` | `( mat -- mat )` | `prim_mat_sigmoid` | `1.0 / (1.0 + exp(-x))` per element |
| `mat-tanh` | `( mat -- mat )` | `prim_mat_tanh` | `std::tanh(x)` per element |
| `mat-hadamard` | `( mat1 mat2 -- mat )` | `prim_mat_hadamard` | `A[i] * B[i]`, dimension check |
| `mat-add-col` | `( mat col -- mat )` | `prim_mat_add_col` | Broadcast-add column vector to every column |
| `mat-randn` | `( rows cols -- mat )` | `prim_mat_randn` | Box-Muller transform using `prng_engine()` |

### Files Modified

- **`src/core/matrix_primitives.cpp`** — Add `// Neural Network` section before `// Registration`. 6 new functions + 6 registrations. Refactor `mat-rand` to use `prng_engine()`.
- **`include/etil/core/primitives.hpp`** — Add `std::mt19937_64& prng_engine();` declaration.
- **`data/help.til`** — 6 new entries (description, stack-effect, category=matrix).
- **`tests/unit/test_matrix_primitives.cpp`** — ~12 new GTest cases.
- **`tests/til/test_matrix.til`** — ~8 new TIL integration tests.

### Implementation Notes

- `mat-add-col`: pop `col` (must be rows x 1), pop `mat`, check `col->rows() == mat->rows() && col->cols() == 1`. Loop: `result[r + c*rows] = mat[r + c*rows] + col[r]`.
- `mat-randn` Box-Muller: generates pairs of normals from pairs of uniforms. For odd-size matrices, generate one extra and discard. Uses `prng_engine()` so `random-seed` controls reproducibility.
- `mat-hadamard`: follows the `mat+` pattern — pop B, pop A, dimension check, element-wise multiply.

---

## Stage 2: Backpropagation Primitives

**Version bump**: v0.8.20

### New Words (8)

| Word | Stack Effect | C++ Function | Implementation |
|------|-------------|--------------|----------------|
| `mat-relu'` | `( mat -- mat )` | `prim_mat_relu_prime` | `x > 0 ? 1.0 : 0.0` per element |
| `mat-sigmoid'` | `( mat -- mat )` | `prim_mat_sigmoid_prime` | `s = sigmoid(x); s * (1-s)` per element |
| `mat-tanh'` | `( mat -- mat )` | `prim_mat_tanh_prime` | `t = tanh(x); 1 - t*t` per element |
| `mat-sum` | `( mat -- scalar )` | `prim_mat_sum` | Sum all elements, push double |
| `mat-col-sum` | `( mat -- col_vec )` | `prim_mat_col_sum` | Sum across columns → (rows x 1) matrix |
| `mat-mean` | `( mat -- scalar )` | `prim_mat_mean` | `sum / size` |
| `mat-clip` | `( mat lo hi -- mat )` | `prim_mat_clip` | `std::clamp(x, lo, hi)` per element |
| `tanh` | `( x -- x )` | `prim_tanh` | Scalar `std::tanh` (goes in `primitives.cpp`) |

### Files Modified

- **`src/core/matrix_primitives.cpp`** — 7 new functions + 7 registrations in the Neural Network section.
- **`src/core/primitives.cpp`** — 1 new scalar `tanh` function + 1 registration (math section, alongside `sin`/`cos`/`tan`).
- **`data/help.til`** — 8 new entries.
- **`tests/unit/test_matrix_primitives.cpp`** — ~14 new GTest cases.
- **`tests/til/test_matrix.til`** — ~6 new TIL integration tests.

### Implementation Notes

- `mat-col-sum`: creates a `(rows x 1)` result. For each row r, sum `mat[r + c*rows]` for all columns c. This is the bias gradient in backprop.
- `tanh` scalar goes in `primitives.cpp` alongside `sin`, `cos`, `tan`, using the existing `pop_as_double` helper.
- `mat-clip`: pop `hi`, pop `lo`, pop mat. Error if `lo > hi`.

---

## Stage 3: Extensibility and Classification

**Version bump**: v0.8.21

### New C++ Words (3)

| Word | Stack Effect | C++ Function | Implementation |
|------|-------------|--------------|----------------|
| `mat-apply` | `( mat xt -- mat )` | `prim_mat_apply` | Execute xt per element |
| `mat-softmax` | `( mat -- mat )` | `prim_mat_softmax` | Column-wise softmax (numerically stable) |
| `mat-cross-entropy` | `( predicted actual -- scalar )` | `prim_mat_cross_entropy` | `-sum(actual * log(pred + 1e-15))` |

### TIL-Level Words (documented as examples in help.til)

- `mat-xavier ( fan_in fan_out -- mat )` — `2dup mat-randn -rot + int->float sqrt 1.0 swap / mat-scale`
- `mat-he ( fan_in fan_out -- mat )` — `2dup mat-randn -rot drop int->float sqrt 2.0 swap / mat-scale`
- `mat-mse ( predicted actual -- loss )` — `mat- dup mat-hadamard mat-mean`

### Files Modified

- **`src/core/matrix_primitives.cpp`** — 3 new functions + 3 registrations.
- **`data/help.til`** — 3 new entries + TIL-level word examples.
- **`tests/unit/test_matrix_primitives.cpp`** — ~10 new GTest cases.
- **`tests/til/test_matrix.til`** — ~4 new TIL integration tests.

### Implementation Notes

- **`mat-apply`** is the most complex: pop xt (must be `Value::Type::Xt`), pop matrix. For each element: push as double, invoke xt (native or bytecode via `execute_compiled`), pop result. The inner `execute_compiled` handles `tick()` for budget enforcement. Does NOT use `enter_call()`/`exit_call()` per element.
- **`mat-softmax`**: column-wise. For each column: find max, subtract max (numerical stability), exp all, divide by column sum.
- **`mat-cross-entropy`**: dimension check, then `-sum(actual[i] * log(pred[i] + 1e-15)) / size`.

---

## Verification

After each stage:

1. **Build**: `ninja -C build-debug && ninja -C build`
2. **Test**: `ctest --test-dir build-debug --output-on-failure && ctest --test-dir build --output-on-failure`
3. **REPL smoke test** (after Stage 1):
```forth
# 2-layer forward pass: 2 inputs → 3 hidden (ReLU) → 1 output (sigmoid)
42 random-seed
array-new 0.5 array-push -0.3 array-push 2 1 mat-from-array
3 2 mat-randn  3 1 mat-new
rot rot mat*  swap mat-add-col  mat-relu
1 3 mat-randn  1 1 mat-new
rot rot mat*  swap mat-add-col  mat-sigmoid
." Output: " mat.
```
4. **Super push** after each stage to deploy via CI pipeline.

## Summary

| Stage | Words | Lines (~) | Tests (~) | Version |
|-------|-------|-----------|-----------|---------|
| 1: Forward Pass | 6 | 120 | 20 | v0.8.19 |
| 2: Backprop | 8 | 130 | 20 | v0.8.20 |
| 3: Extensibility | 3+3 TIL | 100 | 14 | v0.8.21 |
| **Total** | **17 C++ + 3 TIL** | **~350** | **~54** | |
