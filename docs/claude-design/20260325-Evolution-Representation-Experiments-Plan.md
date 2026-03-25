# Evolution Representation Experiments — Plan

**Date:** 2026-03-25
**Design Doc:** `20260325-Evolution-Granularity-Review.md`
**Prerequisites:** v1.9.1 (full logging infrastructure)
**Status:** Planned

---

## Goal

Test three representation approaches that address the mutation granularity problem. Each experiment uses the same target function (`f(x) = x² + 3x + 5`), the same test cases, and the same logging infrastructure, enabling direct comparison.

**Success criterion**: At least one approach shows monotonically improving fitness over 200+ generations, breaking the Gen 2-6 convergence ceiling seen in all previous experiments.

---

## Experiment 1: Neutral Padding

### Concept

Seed the program with identity operations that can be mutated into meaningful computation without breaking the program. These act as "junk DNA" — safe mutation targets.

### Identity Operations in TIL

| No-op Pattern | Effect | What Mutation Can Turn It Into |
|---|---|---|
| `1 *` | Multiply by 1 (identity) | `3 *` (scale by constant) |
| `0 +` | Add 0 (identity) | `5 +` (add constant) |
| `swap swap` | Double swap (identity) | `swap *` (multiply TOS-1 by TOS) |
| `dup drop` | Push and pop (identity) | `dup *` (square) |

### Seed Program

Instead of seeding with `dup +` (2 instructions, no room to maneuver):

```til
: target-fn
  dup 1 * 0 +        # first term: x * 1 + 0 = x
  swap                #
  dup 1 * 0 +        # second term: x * 1 + 0 = x
  +                   # sum: 2x
  0 +                 # final offset: + 0
;
```

This is semantically `2x + 0` — same as `dup +`. But it has **11 instructions** with 6 mutable constants (`1`, `0`, `1`, `0`, `0` and the implicit structure).

The perturb mutation can now modify constants:
- `1 *` → `3 *` (first coefficient)
- `0 +` → `5 +` (additive constant)

The substitute mutation can change structure:
- `dup 1 *` → `dup dup *` (squaring)

### Implementation

No C++ changes needed. This is a TIL-level experiment using the existing AST evolution engine.

```til
: target-fn dup 1 * 0 + swap dup 1 * 0 + + 0 + ;
```

Register with math pool, distance fitness, full logging. Run 500 generations.

### What to Measure

- Does perturb mutation modify the constants? (Log: `[perturb] int 1 → 3`)
- Does fitness improve over generations? (Baseline should climb from 0.18)
- Does the population maintain diversity? (Different impls should have different constants)
- How long until the first exact test pass?

### Estimated Effort

TIL script only — 30 minutes.

---

## Experiment 2: Parametric Templates

### Concept

Separate the program structure (which words, in what order) from the parameters (numeric constants). Evolve parameters via continuous perturb mutation. Evolve structure rarely.

### Implementation Approach

Define the template as a colon definition with `variable` constants:

```til
variable C0  1 C0 !
variable C1  1 C1 !
variable C2  0 C2 !

: target-fn
  dup C0 @ * swap dup C1 @ * + C2 @ +
;
```

The template structure is `dup C * swap dup C * + C +`. Evolution only mutates the constants stored in `C0`, `C1`, `C2`.

### Problem: Current Engine Doesn't Know About Variables

The AST evolution engine operates on bytecode. Variables compile to `PushDataPtr` + `@` (fetch) instructions. The perturb mutation only modifies `PushInt`/`PushFloat` literals in the AST — it doesn't know how to modify values stored in variables.

### Alternative: Inline Constants with High Perturb Rate

Instead of variables, use inline literals with a mutation weight configuration that heavily favors perturb:

```til
: target-fn
  dup 1 * swap dup 1 * + 0 +
;
```

Configure mutation weights: perturb 60%, substitute 10%, grow 5%, shrink 5%, move 5%, control-flow 5%. This makes constant tuning the dominant operation.

### What to Measure

- Perturb frequency vs other mutations (should dominate)
- Constant values converging toward 1, 3, 5 over generations
- Fitness climbing as constants approach target values
- Whether structure mutations disrupt progress (if so, lower their weight further)

### Estimated Effort

TIL script + mutation weight tuning — 1 hour.

---

## Experiment 3: Gene Duplication

### Concept

Duplicate a working sub-expression in the AST, creating a copy that can diverge independently. The original is preserved by selection pressure. The copy is free to explore.

This is fundamentally a **permutation** operation — rearranging and replicating existing working code rather than inventing new code from the dictionary. It scales better than point mutation because the building blocks are proven functional sub-programs.

### Why This is Different from Grow

Grow inserts a **random word** from the pool. Gene duplication inserts a **copy of existing working code**. The difference:

- **Grow**: inserts `abs` (random, might not fit) → often crashes or produces garbage
- **Duplicate**: copies `dup *` (already works in this program) → guaranteed to compile, likely to run

The duplicated code is a **known-good module**. Mutating a known-good module is far more likely to produce a slightly-different-but-still-functional variant than inserting a random word.

### Recursive Duplication and Crossover

Gene duplication becomes especially powerful when combined with crossover. Consider:

```
Parent A: dup +              →  2x
Parent B: dup *              →  x²

Crossover with duplication:
  Take 'dup +' from A, duplicate
  Take 'dup *' from B, insert between copies
  Result: dup + dup * dup +  →  ???  (needs work, but building blocks are real)
```

More precisely, a **recursive wrapping** crossover:
```
Parent A: dup +              →  f(x) = 2x
Parent B: dup *              →  g(x) = x²

Wrap A around B:
  dup B + → dup dup * +      →  x² + x

Wrap B around A:
  dup A * → dup dup + *      →  2x * x = 2x²
```

This is **function composition through structural permutation**. The crossover doesn't randomly splice ASTs — it wraps one program around another, using the existing code as a sub-computation. Each wrapped program is a composite of two working programs.

### Scaling Properties

Gene duplication scales better than point mutation because:

1. **Building blocks grow over time**: Early generations have small sub-programs. Duplication makes medium sub-programs. Duplication of duplicates makes large sub-programs. Complexity emerges from composition, not random assembly.

2. **Known-good modules persist**: The original sub-program survives selection because it works. Only the copy needs to improve. This is a ratchet — complexity can only increase, never lose proven functionality.

3. **Crossover becomes meaningful**: With point mutation, crossover between two random programs produces garbage. With duplication-derived programs, crossover between two programs with overlapping sub-modules produces recombinations of working parts.

4. **Recursive composition**: Wrapping program A around program B produces A(B(x)). Wrapping B around A produces B(A(x)). These are different functions built from the same components. The search space is function compositions, not random word sequences.

### Implementation

New AST mutation operator: `duplicate_subtree()`

```
1. Pick a random sub-expression in the AST (a Sequence child or a sub-tree)
2. Copy it
3. Insert the copy adjacent to the original (before or after)
4. Type repair handles any stack imbalance from the duplication
```

New crossover variant: `wrap_crossover()`

```
1. Decompile both parents → AST_A, AST_B
2. Pick a sub-expression S from parent B
3. Insert S into parent A at a random position
4. The result is A with a sub-module from B embedded in it
5. Type repair + bridge insertion handles type mismatches at the junction
```

### What to Measure

- How often does duplication produce runnable children? (Should be >80%, since duplicated code is known-good)
- Does wrapping crossover produce programs with higher fitness than either parent?
- Do programs grow in complexity through composition over generations?
- Is there a pattern of "copy, mutate copy, select better copy" visible in the logs?

### Estimated Effort

| Task | Solo Human | AI-Assisted |
|---|---|---|
| `duplicate_subtree()` operator | 3 hours | 1 hour |
| `wrap_crossover()` operator | 4 hours | 1.5 hours |
| Experiment scripts | 1 hour | 30 min |
| Analysis and logging review | 2 hours | 30 min |
| **Total** | **~10 hours** | **~3.5 hours** |

---

## Experiment Comparison Framework

All three experiments use identical test configuration:

| Parameter | Value |
|---|---|
| Target | `f(x) = x² + 3x + 5` |
| Test cases | x ∈ {-1, 0, 1, 2, 3, 5, 10} (7 cases) |
| Fitness mode | Distance (α = 1.0) |
| Generations | 500 |
| Population | 10 (default) |
| Pool | Math pool |
| Logging | Granular + Diff + AST dump (full) |
| PRNG seed | 42 (deterministic) |

### Metrics for Comparison

| Metric | Description |
|---|---|
| **Fitness curve** | Plot of best baseline fitness per generation (0 to 500) |
| **Convergence generation** | First generation where fitness stops improving for 50+ gens |
| **Peak fitness** | Best fitness achieved across all generations |
| **Exact passes** | Number of test cases matched exactly (0/7 to 7/7) |
| **Program length** | Mean AST node count over generations (bloat indicator) |
| **Diversity** | Number of distinct fitness values in the population at each generation |
| **Crash rate** | Fraction of children that fail at runtime (target: <10%) |

### Expected Outcomes

| Experiment | Expected Fitness Curve | Crash Rate | Why |
|---|---|---|---|
| Neutral Padding | Slow climb, may plateau at ~0.3 | Low (~10%) | Constants drift toward target; structure preserved |
| Parametric Templates | Steady climb to ~0.5+ | Very low (~2%) | Perturb dominates, gradient-friendly |
| Gene Duplication | Stepped improvements | Low (~15%) | Duplication creates new building blocks; crossover composes them |

### Null Hypothesis

If none of the three approaches break the Gen 2-6 convergence ceiling, the problem is not representation granularity but population dynamics (size 10 is simply too small for any representation). In that case, the next experiment would increase population to 100-1000.

---

## Execution Order

1. **Experiment 1 (Neutral Padding)** — fastest to implement (TIL script only), provides baseline for comparison
2. **Experiment 2 (Parametric Templates)** — TIL script + weight tuning, tests parameter-only evolution
3. **Experiment 3 (Gene Duplication)** — requires new C++ operators, most ambitious but most promising

Each experiment produces a log file for analysis. Results documented in an updated experiments report.

---

## Dependencies

```
Experiment 1 (Neutral Padding)    ← no code changes, TIL script only
Experiment 2 (Parametric)         ← no code changes, TIL script + config
Experiment 3 (Gene Duplication)   ← C++ changes: duplicate_subtree(), wrap_crossover()
                                     depends on diff logging for analysis
```

Experiments 1 and 2 can run immediately. Experiment 3 requires implementation work.
