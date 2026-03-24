# Evolution Experiments Report

**Date:** 2026-03-24
**ETIL Version:** v1.6.3
**Scripts:** `examples/tui/evolve-regression.til`, `evolve-function.til`, `evolve-sort.til`

---

## Summary

Three experiments tested ETIL's evolutionary capabilities on increasingly difficult problems. The built-in AST evolution engine failed on program synthesis tasks, while a manual (1+1) evolutionary strategy succeeded on sorting network optimization. The results expose specific gaps in the AST evolution pipeline that need to be addressed.

| Experiment | Method | Goal | Result |
|---|---|---|---|
| Symbolic Regression | AST evolution | Evolve `f(x) = x² + 3x + 5` | **Failed** — mutations can't grow program structure |
| Function Synthesis | AST evolution | Evolve `f(x,y) = x*y + x - y` | **Failed** — same structural limitation |
| Sorting Network | (1+1) ES | Find valid N=4 sorting network | **Succeeded** — 6-comparator network in 116 generations |

---

## Experiment 1: Symbolic Regression (AST Evolution)

**Goal:** Starting from `: target-fn dup + ;` (computes 2x), evolve a word that matches `f(x) = x² + 3x + 5` using 10 test cases.

**Method:** `evolve-register` with test case maps, `evolve-word` for 100 generations, `select-strategy` set to weighted random.

**Configuration:**
- Initial implementation: `dup +` (2 instructions)
- Test cases: 10 input/output pairs covering x ∈ {-2, -1, 0, 1, 2, 3, 4, 5, 7, 10}
- Population limit: 10 (C++ default)
- Generation size: 5 children per generation
- Mutation rate: 80% mutation, 20% crossover

**Result:** After 100 generations (500 children evaluated), the best implementation outputs `pi` (3.14159) for most inputs. The mutation engine substituted `pi` for the arithmetic operations — a constant function that happens to be close to `f(-1) = 3`.

**Output sample:**
```
f(0)  = 3.14159  (expected 5)
f(1)  = 3.14159  (expected 9)
f(10) = 3.14159  (expected 135)
```

**Errors during evolution:** Hundreds of "Error in 'mat-inv' (stack depth: 1)", "Error in 'lstat' (stack depth: 1)", "Error in 'ssplit' (stack depth: 1)" — the engine substituted file I/O, matrix, and string words into a numeric function.

**Why it failed:** Two structural reasons:

1. **No grow mutation.** The initial program has 2 instructions (`dup`, `+`). The target requires ~8 instructions (`dup dup * swap 3 * + 5 +`). The AST evolution engine can substitute, perturb, and move instructions — but it cannot insert new ones. The program length is fixed at 2 instructions for all 500 children.

2. **Unrestricted word pool.** The substitute mutation draws from the entire 277-word dictionary. For a numeric problem, the relevant search space is ~10 words (`+`, `-`, `*`, `dup`, `swap`, `over`, integer literals). Substituting `mat-inv` or `lstat` into a numeric function is guaranteed to fail, but the engine tries it anyway. The semantic tag system exists but has no math-domain tags.

---

## Experiment 2: Function Synthesis (AST Evolution)

**Goal:** Starting from `: synth-fn + ;` (computes x+y), evolve a word that matches `f(x,y) = x*y + x - y` using 8 test cases with two-input stack signatures.

**Method:** Same as Experiment 1 but with 200 generations.

**Result:** Same failure mode. The engine cannot grow `+` (1 instruction) into `over over * rot + swap -` (~7 instructions). Mutations substitute unrelated words from the full dictionary.

**Conclusion:** AST evolution cannot synthesize programs. It is an optimization engine, not a synthesis engine.

---

## Experiment 3: Sorting Network ((1+1) Evolutionary Strategy)

**Goal:** Evolve a sorting network that correctly sorts all 24 permutations of [1, 2, 3, 4]. The optimal network for N=4 has 5 comparators.

**Method:** Manual (1+1) ES implemented entirely in TIL. The genome is an array of `[i, j]` comparator pairs. Mutation operators: replace a random comparator (50%), add a new comparator (25%), remove the last comparator (25%).

**Configuration:**
- Net size: 4 elements
- Max comparators: 8
- Max generations: 1000
- PRNG seed: 42

**Fitness function:**
```
score = (correctly_sorted_permutations × 100) − number_of_comparators
```
Primary objective: sort all 24 permutations. Secondary: minimize comparator count.

**Result:** Found a valid 6-comparator sorting network in 116 generations:

```
swap-if(2, 3)
swap-if(0, 3)
swap-if(0, 2)
swap-if(1, 2)
swap-if(0, 1)
swap-if(2, 3)
```

This network correctly sorts all 24 permutations of 4 elements. It uses 6 comparators (optimal is 5). A second run with seed 77 also found a 6-comparator solution in 262 generations.

**Fitness progression (seed 42):**
```
Gen 0:   Score -6,   Sorted  0/24, Comps 6
Gen 100: Score 1795, Sorted 18/24, Comps 5
Gen 116: Score 2394, Sorted 24/24, Comps 6  ← converged
```

**Why it succeeded:**

1. **Grow/shrink mutations.** The (1+1) ES can add and remove comparators, allowing the program length to vary. This is the critical capability the AST engine lacks.

2. **Domain-restricted search space.** The mutation operators only produce valid comparator pairs `[i, j]` where `0 ≤ i, j < N`. Every mutation produces a structurally valid genome — no type errors, no stack underflows, no irrelevant words.

3. **Smooth fitness landscape.** Partially-correct sorting networks (18/24 permutations sorted) have proportionally higher fitness than random networks (0/24). The fitness gradient guides search toward the solution.

**Why it didn't find optimal (5 comparators):**

The (1+1) ES with a single-parent mutation-only strategy tends to get stuck in local optima. A 6-comparator solution that sorts 24/24 has score 2394; a 5-comparator solution would score 2395. The search must simultaneously reduce comparator count while maintaining 24/24 correctness — a narrow ridge in the fitness landscape. Crossover between multiple parents, simulated annealing, or island-model parallelism would help.

---

## Conclusions

### AST Evolution Engine: Optimization, Not Synthesis

The AST evolution engine is effective for its designed purpose: **optimizing existing implementations** by substituting equivalent operations (e.g., `mat-sigmoid` → `mat-relu`) and tuning numeric constants (perturb mutation). It is fundamentally incapable of program synthesis because:

1. **Fixed program length.** No grow/shrink mutations. A 2-instruction seed can only produce 2-instruction children.
2. **Unrestricted substitution.** The substitute mutation draws from 277 words. For any specific domain, >95% of substitutions produce type errors or stack underflows. This is "throwing different colors of paint at a barn and expecting the Mona Lisa."
3. **No constant introduction.** The perturb mutation adjusts existing numeric constants but cannot introduce new ones. A program without constants cannot evolve constants.

### What Works: Domain-Specific Manual Evolution

The sorting network experiment demonstrates that evolution works well when:

- The genome representation is **domain-specific** (comparator pairs, not arbitrary bytecode)
- The mutation operators are **domain-aware** (produce only valid comparators)
- The fitness landscape has a **gradient** (partial correctness is rewarded)
- The genome length is **variable** (grow/shrink mutations)

### Recommended Improvements to the AST Evolution Engine

To make AST evolution useful for program synthesis (not just optimization):

1. **Grow mutation** — Insert a new instruction at a random position in the AST. The inserted instruction should be drawn from a domain-restricted pool, not the full dictionary.

2. **Shrink mutation** — Remove a random instruction from the AST. Paired with grow, this enables program length exploration.

3. **Domain-restricted word pools** — Allow `evolve-register` to accept a word pool (array of word names) that limits the substitute mutation search space. For numeric problems: `{+ - * dup swap over rot nip tuck}`. For matrix problems: `{mat* mat+ mat- mat-scale mat-transpose}`.

4. **Constant introduction** — A grow variant that inserts integer/float literals. The perturb mutation can then refine them.

5. **Population diversity** — The current population limit (10) with pruning by weight creates premature convergence. Consider niching, crowding, or island-model strategies to maintain diversity.

These changes would transform the engine from an optimizer into a genuine genetic programming system.

---

## Scripts

All three experiment scripts are in `examples/tui/`:

| File | Method | Status |
|---|---|---|
| `evolve-regression.til` | AST evolution via `evolve-register`/`evolve-word` | Runs, does not converge |
| `evolve-function.til` | AST evolution via `evolve-register`/`evolve-word` | Runs, does not converge |
| `evolve-sort.til` | Manual (1+1) ES with comparator genome | Works — finds valid network |

The sorting network script also demonstrates practical TIL patterns for:
- Building and cloning arrays of arrays
- Variable-based stack management for complex operations
- Fitness-proportional selection
- `begin`/`until` evolution loops with convergence criteria
