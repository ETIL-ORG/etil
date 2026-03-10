# ETIL MLP Design Analysis

**Date:** 2026-03-10  
**Version:** ETIL 0.8.15  
**Subject:** Feasibility and design for a Multilayer Perceptron in ETIL

---

## 1. What You Have

ETIL's matrix subsystem is solid and well-engineered for an MLP. The foundation:

**HeapMatrix** — column-major `double*`, inline dimensions, LAPACK-native layout. The `lda()` accessor and raw `data()` pointer mean zero-copy handoff to BLAS/LAPACK. The reference-counted `HeapObject` base gives you deterministic lifetime with single-cell stack representation. This is the right design.

**BLAS/LAPACK bindings** — `mat*` goes through `cblas_dgemm`, which is the single most important operation for an MLP. Forward propagation through a layer is a matrix multiply, and you're getting hardware-optimized GEMM (OpenBLAS or MKL, depending on what's linked). The solvers (`mat-solve`, `mat-lstsq`, `mat-svd`, `mat-eigen`) aren't directly needed for MLP training but could be useful for diagnostics and initialization.

**Scalar math** — `exp`, `log`, `sqrt`, `pow`, `fmax`, `fmin`, `tanh` (via `tan`? — see gaps below), `abs`. These exist as stack-level primitives operating on single doubles.

**Data structures** — `HeapArray` can hold arbitrary `Value` types including matrices, so a network's layers can be stored as an array of weight matrices. `HeapMap` can serve as a model container with named fields. The `variable` / `constant` words plus `CREATE`/`DOES>` give you named state.

**Control flow** — `do...loop`, `begin...until`, `begin...while...repeat`, `if...else...then`, `recurse`. Enough for training loops and layer iteration.

**Evolution infrastructure** — `WordImpl` already tracks `weight_`, `PerfProfile`, `MutationHistory`, `parent_ids_`, generation, and success/failure counts. The `test_evolve.til` demonstrates a working `(1+1)` evolutionary strategy entirely in ETIL. The `WordConcept` model (one name → multiple weighted implementations) is exactly the right abstraction for evolving alternative MLP architectures or hyperparameters.

---

## 2. What's Missing — The Critical Gaps

### 2.1 Element-wise matrix operations (BLOCKING)

This is the single biggest gap. An MLP requires applying activation functions element-wise across entire matrices. You need:

| Word | Stack Effect | Purpose |
|------|-------------|---------|
| `mat-apply` | `( mat xt -- mat )` | Apply an execution token to every element |
| `mat-hadamard` | `( mat mat -- mat )` | Element-wise multiply (needed for backprop gradients) |
| `mat-add-col` | `( mat vec -- mat )` | Broadcast-add a column vector to every column (bias addition across a batch) |

Without `mat-apply`, you'd have to extract every element with `mat-get`, apply the function, and `mat-set` it back — an O(rows × cols) loop through the interpreter with per-element stack manipulation. For a 256×128 weight matrix, that's 32,768 interpreter cycles versus one C++ loop. This is a ~1000× performance cliff.

**Recommendation:** Implement `mat-apply` as a C++ primitive that accepts an execution token (xt) and calls it per element. For the common activations (ReLU, sigmoid, tanh), also add dedicated C++ primitives `mat-relu`, `mat-sigmoid`, `mat-tanh` that bypass the interpreter entirely and run tight loops over `data()`.

### 2.2 Activation functions

The scalar vocabulary has `exp`, `log`, `sin`, `cos`, `atan` — but critically:

- **No `tanh` on the word list.** You have `tan` but not `tanh`. The hyperbolic tangent is one of the three canonical activation functions.
- **No `sigmoid` word.** Sigmoid is `1 / (1 + exp(-x))` which can be composed, but an optimized primitive avoids the numerical instability for large negative inputs.
- **No `relu` word.** Trivially `0.0 fmax` but a named word is clearer.

For matrix-level activations (which is what matters), you need:

```
mat-relu     ( mat -- mat )    \ max(0, x) element-wise
mat-sigmoid  ( mat -- mat )    \ 1/(1+exp(-x)) element-wise
mat-tanh     ( mat -- mat )    \ tanh(x) element-wise
```

And their derivatives for backpropagation:

```
mat-relu'    ( mat -- mat )    \ 1 if x>0, 0 otherwise
mat-sigmoid' ( mat -- mat )    \ σ(x)·(1-σ(x))
mat-tanh'    ( mat -- mat )    \ 1-tanh²(x)
```

### 2.3 Weight initialization

`mat-rand` generates uniform [0,1]. Neural networks need:

- **Xavier/Glorot:** Normal(0, sqrt(2/(fan_in+fan_out))) — good for sigmoid/tanh
- **He/Kaiming:** Normal(0, sqrt(2/fan_in)) — good for ReLU
- **Normal distribution:** At minimum, `mat-randn` (standard normal)

Without proper initialization, deep networks won't train — gradients either explode or vanish from the first forward pass.

**Recommendation:** Add `mat-randn ( rows cols -- mat )` using Box-Muller transform, plus a `mat-xavier ( fan_in fan_out -- mat )` convenience word.

### 2.4 In-place operations

Every `mat+`, `mat-`, `mat-scale` allocates a **new** HeapMatrix and releases the inputs. In a training loop doing hundreds of thousands of iterations, each with multiple layers of forward pass, backward pass, and weight update, you're allocating and freeing matrices on every operation.

The reference counting guarantees immediate deallocation (no GC pause), so freed memory is immediately reusable. But the allocation pressure is still significant — `new HeapMatrix` → `vector<double>` allocation → zero-fill → compute → release → `delete` → vector dealloc.

**Recommendation for later:** Add in-place variants: `mat+!`, `mat-!`, `mat-scale!` that mutate the top-of-stack matrix directly (asserting refcount == 1 for safety). This is an optimization — the functional versions work correctly, they're just slower.

### 2.5 Broadcasting (bias addition)

MLP forward pass for a batch: `Y = W × X + b`, where `X` is `(input_dim × batch_size)`, `W` is `(output_dim × input_dim)`, and `b` is `(output_dim × 1)`. The bias `b` must be added to every column of the result.

Currently `mat+` requires identical dimensions. You need either:

- `mat-add-col ( mat col_vec -- mat )` — add column vector to every column
- Or a general broadcasting `mat+` that detects the (n×m) + (n×1) case

**Recommendation:** `mat-add-col` as a dedicated primitive. It's a tight loop: for each column j, add `b[i]` to `result[i,j]`. Clean, no ambiguity.

### 2.6 Loss functions

You need at least one loss function to train:

- `mat-mse ( predicted actual -- scalar )` — mean squared error
- `mat-softmax ( mat -- mat )` — column-wise softmax for classification
- `mat-cross-entropy ( predicted actual -- scalar )` — cross-entropy loss

### 2.7 Matrix slicing / column extraction as matrix

`mat-col` currently returns a `HeapArray`, not a `HeapMatrix`. For MLP work, you often want to extract a column and keep it as a matrix (column vector). Converting through HeapArray loses the BLAS compatibility.

**Recommendation:** Add `mat-col-vec ( mat j -- mat )` that returns an (n×1) HeapMatrix.

---

## 3. Proposed MLP Word Vocabulary

Organized into what can be built in ETIL today versus what needs new C++ primitives:

### 3.1 New C++ primitives required

```forth
\ --- Element-wise operations (CRITICAL) ---
mat-relu       ( mat -- mat )           \ max(0,x) element-wise
mat-sigmoid    ( mat -- mat )           \ 1/(1+exp(-x)) element-wise
mat-tanh       ( mat -- mat )           \ tanh(x) element-wise
mat-relu'      ( mat -- mat )           \ ReLU derivative
mat-sigmoid'   ( mat -- mat )           \ sigmoid derivative
mat-tanh'      ( mat -- mat )           \ tanh derivative
mat-hadamard   ( mat1 mat2 -- mat )     \ element-wise multiply
mat-apply      ( mat xt -- mat )        \ apply xt to each element

\ --- Broadcasting ---
mat-add-col    ( mat col -- mat )       \ broadcast-add column vector

\ --- Initialization ---
mat-randn      ( rows cols -- mat )     \ standard normal distribution
mat-clip       ( mat lo hi -- mat )     \ clamp elements to [lo,hi]

\ --- Reduction ---
mat-sum        ( mat -- scalar )        \ sum all elements
mat-col-sum    ( mat -- col_vec )       \ sum each column → (rows×1)
mat-mean       ( mat -- scalar )        \ mean of all elements
```

### 3.2 ETIL-level words (buildable with existing + new primitives)

```forth
\ --- Initialization ---
: mat-xavier   ( fan_in fan_out -- mat )
    2dup + int->float sqrt 2.0 swap / 
    >r mat-randn r> mat-scale ;

: mat-he       ( fan_in fan_out -- mat )  \ He init (fan_in only)
    swap dup int->float sqrt 2.0 swap /
    >r mat-randn r> mat-scale ;

\ --- Loss ---
: mat-mse      ( predicted actual -- loss )
    mat- dup mat-hadamard mat-mean ;

\ --- Layer forward pass ---
: layer-forward ( X W b activation-xt -- Y )
    >r >r                    \ stash bias and activation
    mat* r> mat-add-col      \ Y = W*X + b
    r> execute ;             \ apply activation

\ --- Training step (SGD) ---
: sgd-update   ( W grad lr -- W' )
    mat-scale mat- ;         \ W' = W - lr * grad
```

### 3.3 Network as data structure

A natural ETIL representation for an MLP:

```forth
\ A layer is a map: { "W": matrix, "b": matrix, "act": xt }
: make-layer  ( fan_in fan_out act-xt -- layer )
    >r
    map-new
    >r
    2dup mat-xavier r@ s" W" swap map-set drop
    swap drop 1 mat-randn 0.0 mat-scale  \ zero bias (fan_out × 1)
    r@ s" b" swap map-set drop
    r> r> swap s" act" swap map-set drop ;

\ A network is an array of layers
: make-mlp  ( in h1 h2 out -- network )
    array-new >r
    \ Layer 1: in → h1 (ReLU)
    ['] mat-relu make-layer r@ swap array-push
    \ Layer 2: h1 → h2 (ReLU)
    ['] mat-relu make-layer r@ swap array-push
    \ Layer 3: h2 → out (sigmoid)
    ['] mat-sigmoid make-layer r@ swap array-push
    r> ;
```

---

## 4. Forward Pass Design

With the proposed primitives, a full forward pass through an MLP:

```forth
\ forward-pass ( X network -- Y activations )
\ Stores intermediate activations for backprop
: forward-pass ( X network -- Y activations )
    array-new >r              \ activations accumulator
    swap                      \ ( network X )
    over array-length 0 do    \ for each layer
        dup r@ swap array-push  \ save activation input
        over i array-get      \ get layer map
        dup s" W" map-get     \ get weight matrix
        swap s" b" map-get    \ get bias
        rot                   \ ( layer_map W b X )
        >r >r                 \ stash W, b
        r> mat*               \ X' = W * X
        r> mat-add-col        \ X' = X' + b
        \ get activation function from layer
        swap s" act" map-get
        execute               \ X' = act(X')
        nip                   \ drop layer map
    loop
    nip                       \ drop network
    r> ;                      \ ( Y activations )
```

---

## 5. Backpropagation Design

Backprop requires the activation derivatives and the Hadamard product. With the proposed primitives:

```forth
\ For each layer (reverse order):
\   dZ = dA ⊙ act'(Z)          — Hadamard with activation derivative
\   dW = (1/m) * dZ * A_prev^T  — weight gradient
\   db = (1/m) * col-sum(dZ)    — bias gradient
\   dA_prev = W^T * dZ           — propagate gradient backward

: backprop-layer ( dA Z A_prev W act'-xt -- dA_prev dW db )
    >r >r >r >r
    r> execute          \ dZ = act'(Z) → apply derivative to Z
    mat-hadamard        \ dZ = dA ⊙ act'(Z)
    dup r>              \ ( dZ dZ A_prev )
    mat-transpose mat*  \ dW = dZ * A_prev^T
    \ ... scale by 1/m, compute db, compute dA_prev
;
```

---

## 6. Evolutionary Angle

This is where ETIL's architecture becomes genuinely interesting for neural networks. The `WordConcept` → multiple `WordImpl` model maps naturally onto neural architecture search:

**Evolving activation functions:** Register multiple implementations of an `activate` concept:
```forth
\ Implementation 1: ReLU (weight 0.4)
: activate mat-relu ;

\ Implementation 2: sigmoid (weight 0.3)
: activate mat-sigmoid ;

\ Implementation 3: tanh (weight 0.3)
: activate mat-tanh ;
```

The selection engine picks which activation to use based on performance metrics. Over time, the weights shift toward whichever activation yields better loss curves for the actual data distribution.

**Evolving network topology:** Each MLP configuration (layer count, width per layer, activation per layer) is a "genome." The existing `(1+1)` evolution strategy in `test_evolve.til` already demonstrates the pattern — generate, mutate, evaluate fitness, select. Fitness is validation loss.

**Evolving learning rate schedules:** Multiple implementations of an `lr-schedule` word, each with different decay curves. The `PerfProfile` on each `WordImpl` tracks execution time and success rate — these feed directly into selection pressure.

---

## 7. Implementation Priority

Ordered by impact on getting a working MLP:

| Priority | Item | Effort | Impact |
|----------|------|--------|--------|
| **P0** | `mat-relu`, `mat-sigmoid`, `mat-tanh` | Small (tight C++ loops) | Forward pass |
| **P0** | `mat-hadamard` | Small | Backprop |
| **P0** | `mat-add-col` | Small | Bias addition |
| **P0** | `mat-randn` | Small (Box-Muller) | Weight init |
| **P1** | `mat-relu'`, `mat-sigmoid'`, `mat-tanh'` | Small | Backprop derivatives |
| **P1** | `mat-sum`, `mat-col-sum` | Small | Loss functions, bias gradients |
| **P1** | `mat-clip` | Small | Gradient clipping |
| **P1** | `tanh` scalar word | Trivial | Completeness |
| **P2** | `mat-apply` (xt-based) | Medium | Extensibility |
| **P2** | `mat-xavier`, `mat-he` | ETIL-level | Best-practice init |
| **P2** | In-place variants (`mat+!` etc.) | Medium | Training performance |
| **P3** | `mat-softmax`, `mat-cross-entropy` | Medium | Classification tasks |
| **P3** | Mini-batch data loading words | Medium | Practical training |

The P0 items are roughly 200-300 lines of C++ in `matrix_primitives.cpp` following the existing patterns. Each is a tight loop over `mat->data()` with no LAPACK dependency. A focused session with Claude Code could deliver all four in an afternoon.

---

## 8. Architectural Observations

**The code is clean and consistent.** Every matrix primitive follows the same pattern: pop arguments, validate dimensions, allocate result, compute, push result. Error handling is thorough — dimension mismatches, null pointers, and bounds are all checked with informative messages. The reference counting protocol (pop transfers ownership, caller releases) is applied uniformly.

**The `ETIL_LINALG_ENABLED` guard is good.** The entire matrix subsystem compiles out cleanly when linear algebra support isn't available. The MLP words should follow the same pattern.

**`mat*` via DGEMM is the right call.** This one decision means ETIL's forward pass performance will be competitive with NumPy/PyTorch for matrix-multiply-dominated workloads (which MLPs are). The rest is overhead — and the proposed element-wise primitives eliminate the main source of that overhead.

**Column-major HeapMatrix + BLAS interop is a genuine advantage.** Many "embed LAPACK in language X" projects fight the column-major layout. ETIL embraced it from the start, and `mat-from-array` handles the row-major → column-major translation at the boundary where users think in row-major. That's the right place to pay the cost.

**The evolution/selection directories are still `.gitkeep` placeholders.** The MLP design should assume these modules don't exist yet and define the network purely in terms of existing words + the proposed new primitives. When the evolution engine materializes, the MLP's structure (layers as array of maps, activation as execution tokens) is already compatible with evolutionary selection.

**Allocation pressure is the long-term concern.** Every operation creates a new matrix. For training loops with thousands of iterations across multiple layers, this will dominate. But it's a performance optimization, not a correctness issue — get the MLP working first with functional (allocating) operations, then add in-place variants where profiling shows it matters.
