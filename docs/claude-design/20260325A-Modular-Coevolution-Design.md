# Modular Co-Evolution: Evolving Concept Chains, Not Monolithic Programs

**Date:** 2026-03-25
**References:** `20260325-Evolution-Granularity-Review.md`, ETIL CLAUDE.md (Core Innovation section)
**Status:** Design Proposal

---

## Motivation

ETIL's core innovation is a dictionary where each word concept can have multiple implementations, selected at runtime by performance profiling, machine learning, or genetic evolution. The architecture was designed for exactly this — yet the evolution engine has been evolving monolithic programs within a single concept.

A 2-instruction program mutated at the word level changes 50% of its genome per generation. A 10-instruction program mutates at 10%. There is no buffering, no modularity, no preservation of working sub-components across mutations. Every experiment flatlines by generation 6 because the population converges to a single phenotype with no way to make incremental improvements.

The insight: **stop evolving one concept with 10 monolithic implementations. Instead, chain multiple concepts together and evolve each concept's implementation population independently.** This is what the dictionary architecture was built for.

---

## The Monolithic Approach (Current)

```til
: target-fn dup dup * swap 3 * + 5 + ;
```

One concept (`target-fn`), 10 implementations, each a complete standalone program. Mutating any implementation changes the entire computation. A good partial solution (e.g., correct `x²` term) is destroyed when the `3x` term is mutated.

```
Dictionary:
  target-fn
    ├── impl#1: dup +                    (= 2x)
    ├── impl#2: dup *                    (= x²)
    ├── impl#3: dup negate +             (= 0)
    ├── impl#4: dup abs                  (= |x|)
    └── ... (10 total, all competing as complete solutions)
```

Problem: impl#2 has a correct `x²` but no way to preserve it while exploring the `3x + 5` part. The next mutation replaces `*` with `-` and the `x²` is gone.

---

## The Modular Approach (Proposed)

Decompose the target into a chain of concepts. Each concept is a small, independently evolvable module:

```til
: square-term dup * ;            # concept: first term
: linear-term dup 3 * ;          # concept: second term
: offset 5 ;                     # concept: constant offset
: target-fn square-term swap linear-term + offset + ;
```

```
Dictionary:
  square-term                         linear-term
    ├── impl#1: dup *     (x²)         ├── impl#1: dup 3 *    (3x)
    ├── impl#2: dup +     (2x)         ├── impl#2: dup +      (2x)
    ├── impl#3: dup abs   (|x|)        ├── impl#3: dup 2 *    (2x)
    └── ...                             └── ...

  offset                              target-fn
    ├── impl#1: 5                       └── impl#1: square-term swap
    ├── impl#2: 3                                   linear-term + offset +
    ├── impl#3: 0                       (structure is fixed; sub-concepts evolve)
    └── ...
```

### What This Changes

**Mutation granularity**: Mutating `square-term` changes 1 of 4 concepts (25%), and within `square-term` the mutation changes 1 of 2 instructions (50% of a 2-instruction module, but only 12.5% of the overall computation). Working modules in other concepts are preserved.

**Module preservation**: A correct `square-term = dup *` survives indefinitely because it's a separate concept with its own weight. Even if all `linear-term` impls are garbage, the good `square-term` persists.

**Natural selection granularity**: The selection engine evaluates `target-fn`, which calls `square-term` → `linear-term` → `offset`. Different runs pick different impl combinations. Over time, impls that contribute to higher chain fitness accumulate weight. The selection engine already does this — `WeightedRandom` picks the highest-weight impl at each call site.

---

## How It Maps to ETIL's Existing Architecture

| ETIL Feature | How It's Used |
|---|---|
| Multiple impls per concept | Each sub-concept has its own population of implementations |
| `WeightedRandom` selection | Picks best impl of each sub-concept at runtime |
| `evolve-word` | Evolves one sub-concept's population per call |
| `evolve-register` test cases | Test cases registered on the chain concept (`target-fn`) |
| Fitness evaluation | Evaluates the chain; sub-concept selection happens implicitly |
| Weight normalization | Impls that participate in high-scoring chains accumulate weight |

Almost everything is already built. The dictionary, selection engine, evolution engine, and fitness evaluator all work at the concept level. The missing pieces are at the orchestration layer.

---

## Credit Assignment

The central challenge: when a chain scores 0.5, how much credit does each sub-concept's current impl deserve?

### Implicit Credit (Simplest)

The existing weight system handles this implicitly:

1. `evolve-word "square-term"` creates 5 new impls of `square-term`
2. For each new impl, evaluate `target-fn` (which calls `square-term`, `linear-term`, `offset`)
3. The fitness of `target-fn` becomes the weight of the new `square-term` impl
4. Selection engine picks `square-term` impls with higher weights more often
5. Over generations, good `square-term` impls accumulate weight

The credit assignment is coarse — a `square-term` impl gets full chain fitness, not its marginal contribution. But it's statistically sound over many evaluations: a good `square-term` will consistently participate in higher-scoring chains than a bad one, because the selection engine picks good `linear-term` and `offset` impls alongside it.

### Co-Evolutionary Credit

Evaluate each sub-concept impl with multiple partner combinations:

1. For each `square-term` impl, run `target-fn` with N different `linear-term` × `offset` combinations
2. The `square-term` impl's weight = mean fitness across all partner combinations
3. This isolates the sub-concept's contribution from its partners

More accurate but N× more expensive. With 3 sub-concepts × 10 impls each = 1000 combinations per full evaluation.

### Recommendation

Start with implicit credit. It's free — no code changes to the fitness evaluator. If sub-concept weights oscillate (A is good with B but bad with C, weight bounces), upgrade to co-evolutionary credit.

---

## Chain Structure: Fixed vs Evolved

### Fixed Chain (Phase 1)

The user defines the chain structure manually:

```til
: square-term dup * ;
: linear-term dup 3 * ;
: offset 5 ;
: target-fn square-term swap linear-term + offset + ;
```

The chain structure (`target-fn`) is fixed. Only the sub-concept implementations evolve. This is the simplest starting point and is analogous to neural architecture search where the architecture is fixed but the weights are learned.

### Evolved Chain (Phase 2, future)

The chain structure itself becomes evolvable:

```til
# Original chain:
: target-fn square-term swap linear-term + offset + ;

# Mutated chain (added a new sub-concept call):
: target-fn square-term swap linear-term + cubic-term + offset + ;
```

This adds/removes sub-concept calls from the chain. The chain itself is a concept that can have multiple implementations with different structures. This is analogous to neural architecture search where both the architecture AND the weights are evolved.

Much more complex. Defer to Phase 2.

---

## Orchestration: Evolving the Chain

The evolution loop changes from:

```
# Current: evolve one monolithic concept
100 0 do s" target-fn" evolve-word drop loop
```

To:

```
# Proposed: round-robin evolve sub-concepts
: evolve-chain
  100 0 do
    s" square-term" evolve-word drop
    s" linear-term" evolve-word drop
    s" offset" evolve-word drop
  loop
;
evolve-chain
```

Each `evolve-word` call evaluates `target-fn` (the chain) to get fitness, but only mutates the named sub-concept. The other sub-concepts use their current best impls via the selection engine.

This is **round-robin co-evolution**: each sub-concept gets one generation of mutation per round, while the others are held stable. This prevents the instability of mutating everything simultaneously.

### Alternative: Proportional Evolution

Allocate more evolution budget to sub-concepts with lower fitness:

```
# Evolve underperforming sub-concepts more
if square-term fitness < linear-term fitness
  evolve square-term 3 times
  evolve linear-term 1 time
```

This focuses computational resources where they're most needed. A sub-concept that's already converged doesn't need more mutations.

---

## Biological Parallel

This approach mirrors biological organization:

| Biology | ETIL Modular Co-Evolution |
|---|---|
| Organism | Chain (`target-fn`) |
| Organ | Sub-concept (`square-term`, `linear-term`, `offset`) |
| Organ variant (allele) | Implementation of a sub-concept |
| Genome | Set of all sub-concept impl selections |
| Organism fitness | Chain fitness (measured at `target-fn` level) |
| Organ fitness | Implicit from chain fitness via weight accumulation |
| Sexual reproduction | Crossover = swap sub-concept impls between chains |
| Speciation | Different chain structures (Phase 2) |

In biology, organs evolve semi-independently. A mutation in the heart gene doesn't affect the lung gene. Heart variants compete with other heart variants, not with lungs. But the whole organism's fitness determines which heart variant survives.

This is exactly what modular co-evolution does: sub-concepts evolve independently, but chain-level fitness drives selection.

---

## Crossover Becomes Natural

With monolithic programs, crossover is fragile — splicing the middle of program A into program B usually produces garbage.

With concept chains, crossover is **module swapping**:

```
Chain A: square-term#4  + linear-term#7  + offset#2   → fitness 0.6
Chain B: square-term#9  + linear-term#3  + offset#5   → fitness 0.4

Crossover: square-term#4 + linear-term#3 + offset#2   → fitness ???
```

Swap one sub-concept's impl between two chains. Each sub-concept is a self-contained module with a defined interface (stack effect). Swapping modules at concept boundaries is always structurally valid — no type repair needed.

This is like sexual reproduction in biology: each parent contributes a variant of each gene. The offspring is a recombination of parental variants, not a splice of parental DNA at random positions.

---

## Relationship to Gene Duplication

Gene duplication (from the Representation Experiments Plan) fits naturally into modular co-evolution:

1. **Duplicate a sub-concept**: create `square-term-2` as a copy of `square-term`
2. **Add it to the chain**: `target-fn` now calls both `square-term` and `square-term-2`
3. **Diverge**: `square-term-2` evolves independently while `square-term` is preserved
4. **Result**: the chain gains a new module with a proven starting point

This is how biology creates new organs: gene duplication → functional divergence → new capability. The chain structure grows in complexity through duplication, not random insertion.

---

## Implications for Population Size

Modular co-evolution partially mitigates the population size problem:

- **Monolithic**: 10 impls of one concept = 10 candidate solutions
- **Modular (3 concepts)**: 10 impls × 3 concepts = 30 candidate modules, combinable into 10³ = 1000 candidate chains

The effective population size for the chain is the *product* of sub-concept population sizes, not the sum. Three concepts with 10 impls each explore 1000 combinations — a 100× increase in effective population without increasing memory or computational cost proportionally.

This is the combinatorial advantage of modularity.

---

## Open Questions

1. **How many sub-concepts?** Too few = still too coarse. Too many = credit assignment becomes noisy. The right granularity depends on the problem.

2. **Who defines the decomposition?** Phase 1: the user. Phase 2: the engine discovers it through gene duplication and chain structure evolution.

3. **Co-evolution stability?** If `square-term` evolves to work well with `linear-term#7`, and then `linear-term#7` is replaced by `linear-term#12`, `square-term` may suddenly perform poorly. This is the "Red Queen" problem in co-evolution. Round-robin scheduling and implicit credit assignment should mitigate it.

4. **How does this interact with the type-directed bridge system?** Sub-concepts have defined stack effects. The chain structure defines how they compose. Type bridges at concept boundaries (where one sub-concept's output feeds another's input) are a natural extension.

5. **Can the evolution engine discover the right decomposition?** This is the Phase 2 question. If the engine can evolve chain structure alongside sub-concept implementations, it becomes a full program synthesis system — discovering both the modular architecture and the module implementations simultaneously.

---

## Summary

ETIL's dictionary architecture — multiple implementations per word concept with runtime selection — was designed for exactly this use case. Modular co-evolution uses the existing infrastructure (dictionary, selection engine, evolution engine, fitness evaluator) to evolve programs as chains of independently evolvable modules rather than monolithic blocks.

The key benefits are finer mutation granularity, module preservation, natural crossover, and combinatorial population amplification. The key risk is credit assignment instability, mitigated by round-robin scheduling and implicit credit through the existing weight system.

This is not a new feature to build — it's the intended use of the architecture, applied to evolution for the first time.
