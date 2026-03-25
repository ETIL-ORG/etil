# Evolution Granularity Review: Why ETIL Mutations Are Organ Transplants

**Date:** 2026-03-25
**References:** All evolution experiment reports and design docs from 2026-03-24/25
**Status:** Review / Architectural Analysis

---

## Summary

After implementing five phases of evolution engine improvements (logging, semantic tags, grow/shrink, sibling pools, distance fitness) and running extensive experiments with full diagnostic logging, a fundamental problem has become clear. The improvements are good engineering but they optimize the wrong level of abstraction. **ETIL is evolving as if it were losing and adding whole limbs, heads, and organs.** The mutation granularity is wrong.

---

## The Evidence

### Experiment Results

| Experiment | Generations | Best Fitness | Converged? |
|---|---|---|---|
| Symbolic regression (no pool) | 100 | 0.107 (flatline Gen 2) | No — premature convergence |
| Symbolic regression (math pool) | 500 | 0.180 (flatline Gen 6) | No — premature convergence |
| Symbolic regression (distance fitness) | 200 | 0.180 (flatline Gen 1) | No — crash floor dominates |
| Function synthesis (math pool) | 200 | 2/5 pass (plateau) | No — structural barrier |
| Sorting network (manual 1+1 ES) | 116 | 24/24 sorted | **Yes** — smooth gradient |

The sorting network succeeded because its genome representation (comparator pairs) supports fine-grained, always-valid mutations. The AST evolution experiments all failed because the mutation granularity is too coarse.

### What the Logs Show

100 generations of no-pool evolution with full diagnostic logging:

- **402 successful mutations, 0 rejected by type repair** — mutations compile but crash at runtime
- **90%+ of children crash** — substituting `+` (Integer, Integer) with `copy-file` (String, String) is structurally valid but semantically nonsensical
- **Fitness flatline at Gen 2** — all 10 individuals converge to essentially the same program; no selection pressure remains
- **Zero type repair interventions** — depth-only checking catches nothing

### The Granularity Numbers

| System | Genome Size | Mutation Size | Mutation as % of Genome |
|---|---|---|---|
| Human biology | 3 billion bp | 1 nucleotide | 0.00000003% |
| E. coli | 4.6 million bp | 1 nucleotide | 0.00002% |
| ETIL (2-word program) | 2 instructions | 1 instruction | **50%** |
| ETIL (10-word program) | 10 instructions | 1 instruction | **10%** |
| ETIL (30-word program) | 30 instructions | 1 instruction | **3.3%** |

Even a 30-word program mutates at 3.3% per generation — orders of magnitude coarser than biology. And each "instruction" in ETIL is a complete computation (matrix inverse, string split, array reduce), not a primitive operation.

---

## Why Biology Works

Four properties of biological evolution that ETIL lacks:

### 1. Neutral Mutations

~98% of human DNA is non-coding. Most nucleotide changes do nothing. Synonymous codons (multiple DNA sequences encoding the same amino acid) provide another layer of neutrality. Conservative amino acid substitutions (replacing one hydrophobic amino acid with another) often preserve protein function.

**In ETIL**: Every instruction is functional. Every change alters the computation. There is zero neutrality. Zero buffer. Every mutation is a phenotype change.

### 2. Tiny Perturbations

A nucleotide change is the smallest possible alteration to the genome. It might change one amino acid in one protein, which might slightly alter one enzyme's activity, which might slightly shift one metabolic rate. The organism is 99.9999999% the same as its parent.

**In ETIL**: The smallest mutation replaces one word — an entire computation. `+` → `*` changes the function from `2x` to `x²`. Not a small perturbation. A completely different function. There is nothing smaller to change.

### 3. Massive Populations

Nature runs millions to billions of individuals per generation. Rare beneficial mutations occur frequently in absolute numbers. Diversity is maintained by sheer population size — one bad generation cannot eliminate all variants.

**In ETIL**: Population 10. One bad generation collapses diversity to near-zero. Beneficial mutations that occur with probability 1/1000 may never appear in 500 generations × 5 children = 2500 evaluations.

### 4. Deep Time

Complex adaptations require long accumulation chains — each step tiny, each step preserved by selection before the next step occurs. The evolution of the eye took ~500,000 years of incremental improvements, each providing slightly better light sensitivity.

**In ETIL**: 500 generations. Each generation mutates at 10-50% of genome. No time for incremental accumulation. The engine needs to leap from `dup +` to `dup dup * swap 3 * + 5 +` in a few steps, each step a major restructuring.

---

## The CISC Instruction Problem

ETIL words are CISC instructions — each performs a complete, high-level operation:

| ETIL Word | What It Does | Biological Equivalent |
|---|---|---|
| `mat-inv` | Full matrix inversion | Entire digestive system |
| `ssplit` | Split string into array | Entire nervous system |
| `array-reduce` | Fold array with function | Entire immune system |
| `+` | Add two numbers | One enzyme |

Substituting `mat-inv` for `+` is like replacing an animal's enzyme with its entire digestive system. The surrounding organism can't accommodate the change. It dies.

Even within the math domain, `+` → `*` is not a small change. Addition and multiplication are fundamentally different operations. In biology, the equivalent would be changing `enzyme-A` to `enzyme-B` — if they catalyze the same reaction at different rates, the organism survives. If they catalyze completely different reactions, it doesn't.

The stack machine architecture compounds the problem. Every word implicitly consumes and produces stack items. There's no way to make a word do "almost the same thing but slightly different." It either computes `a + b` or `a * b` — nothing in between.

---

## The Representation Problem

The improvements we've built address symptoms, not the root cause:

| Improvement | What it fixes | Root cause it doesn't address |
|---|---|---|
| Semantic tags | Prevents `+` → `mat-inv` | `+` → `*` is still a 50% genome change |
| Sibling pools | Restricts to domain-relevant words | Still swapping entire organs within a domain |
| Grow/shrink | Enables length exploration | Inserted words are complete operations |
| Distance fitness | Provides gradient for valid programs | Most mutations still produce crashing programs |
| Type-directed bridges | Eliminates crash floor | Mutations are still coarse-grained |
| Diff logging | Makes the problem visible | Doesn't fix it |

**The root cause is the representation itself.** A flat sequence of CISC words is the wrong genome for evolution. The genotype-phenotype mapping is too direct, too brittle, and provides zero buffering.

---

## Three Paths Forward

The following approaches address the root cause by changing the representation to enable finer-grained, more neutral mutations:

### Path 1: Neutral Padding

Seed programs with no-op instructions that can be mutated without breaking the computation:
```
dup 1 * 0 + swap swap +    # semantically identical to: dup +
```

The `1 *` and `0 +` are neutral DNA — mutation targets that can absorb changes. `1 *` → `3 *` is a small, safe, parametric change. The program structure is preserved.

### Path 2: Separate Structure from Parameters

Represent programs as templates with tunable parameters:
```
Template:    dup C0 * swap C1 * + C2 +
Parameters:  [1, 1, 0]  →  2x + 0  =  2x
```

Parameter mutation is continuous and gradient-friendly. Structure mutation is rare. This mirrors biology where most evolution is allele frequency shifts (parameter tuning), not new body plans (structural innovation).

### Path 3: Gene Duplication

Duplicate working sub-expressions, then diverge the copy:
```
dup +                          →  2x
dup dup + swap dup +           →  duplicate: 2x computed twice
dup dup * swap dup +           →  diverge: x² and 2x
dup dup * swap dup + +         →  combine: x² + 2x
```

This is how biology actually innovates — not through random search, but through duplication of working modules with gradual modification of the copies. The original is preserved by selection. The copy is free to explore.

Gene duplication is fundamentally a **permutation** operation, not a modification. It rearranges and replicates existing working code rather than inventing new code from scratch. This scales better than point mutation because the building blocks are proven functional sub-programs, not random words from a dictionary.

---

## Conclusion

The evolution engine's mutation operators, fitness function, logging, pools, and bridges are all sound engineering. But they're building a better organ-transplant surgery when what the system needs is a finer-grained genome.

The most impactful next step is not another mutation operator or fitness enhancement — it's changing the representation so that mutations can be small, neutral, and parametric. The three paths (neutral padding, parametric templates, gene duplication) each address this from a different angle. They should be evaluated experimentally.
