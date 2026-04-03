# Evolution Priority Review: Type-Directed Bridges vs. Modular Co-Evolution

**Date:** 2026-04-03
**References:** `20260325-Type-Directed-Bridges-Design.md`, `20260325-Modular-Coevolution-Design.md`, `20260325-Distance-Based-Fitness-Design.md`, `20260324-Evolution-Experiments-Report.md`
**Status:** Review / Decision Record

---

## Purpose

The evolution engine has two major design proposals pending implementation. Both target distinct failure modes discovered in the v1.7.0–v1.9.3 experiment series. This document analyzes their dependencies, current implementation state, and interaction effects to determine which should be addressed first.

| Design | Document | Targets |
|---|---|---|
| Type-Directed Bridges (TDB) | `20260325-Type-Directed-Bridges-Design.md` | 90% mutation crash rate (the crash floor) |
| Modular Co-Evolution (MCE) | `20260325-Modular-Coevolution-Design.md` | Monolithic convergence stall at Gen 6 |

---

## Two Root Causes, Two Solutions

The evolution experiments identified two distinct failure modes, each requiring a different fix:

| Failure Mode | Design | Operates At |
|---|---|---|
| 90% of mutations crash at runtime, creating a flat fitness floor at 0.1 that drowns out the distance gradient | Type-Directed Bridges | **Mutation level** — each substitution/grow is type-safe |
| Working sub-components are destroyed by mutations to other parts of the program; population converges to near-identical programs by Gen 6 | Modular Co-Evolution | **Structural level** — program decomposed into independently evolvable modules |

---

## Prerequisite Analysis: Distance-Based Fitness

Distance-based fitness (designed in `20260325-Distance-Based-Fitness-Design.md`) is **fully implemented** as of v1.9.1:

- `FitnessMode::Binary` / `FitnessMode::Distance` enum (`fitness.hpp:15-18`)
- `value_distance()` with numeric cross-promotion, boolean, and type mismatch penalty (`fitness.cpp:32-56`)
- `run_single_test_distance()` with stack depth penalty, element-wise distance, exact match tracking (`fitness.cpp:130-196`)
- `evaluate()` dispatches on mode, computes `1/(1+alpha*dist)` per test case (`fitness.cpp:251`)
- `mean_distance` tracked in `FitnessResult` (`fitness.hpp:33`)
- Wired into evolution engine at `evolution_engine.cpp:93` and `evolution_engine.cpp:173`

**Conclusion from the Bridges design:** Distance fitness is "necessary but not sufficient." It gives `dup +` a score of 0.18 instead of 0.0 — a real gradient exists. But with 90% of mutations crashing (fitness 0.1 floor from the speed component), scores of 0.08–0.12 for valid-but-wrong programs are nearly indistinguishable from the crash floor. The gradient is real but invisible to selection.

---

## Dependency Analysis

### Does MCE depend on TDB?

**Yes, in practice.** MCE decomposes a monolithic program into sub-concepts (`square-term`, `linear-term`, `offset`). But each sub-concept is still evolved by the same mutation operators. When `evolve-word "square-term"` mutates `dup *`, `substitute_call()` still picks replacements by stack depth, not type. Within `square-term`, `*` can be replaced by `copy-file` — same `(2,1)` signature, incompatible types. The crash floor persists inside each module.

MCE reduces the **blast radius** of mutations (only one module changes per generation) but doesn't eliminate crashes. The combinatorial space of 1000 sub-concept combinations is mostly dead programs if 90% of individual impls crash.

### Does TDB depend on MCE?

**No.** TDB operates entirely within the mutation pipeline. It fixes `substitute_call()` and `grow_node()` to filter by input types, regardless of whether the program is monolithic or modular.

### Are they independent?

Architecturally independent — neither requires the other's code. But operationally, TDB is a prerequisite for MCE to deliver its combinatorial advantage. MCE amplifies the search space; TDB ensures that search space is populated with runnable programs.

---

## Current Implementation State

### Type-Directed Bridges — Infrastructure Audit

| Component | File(s) | Current State | TDB Needed |
|---|---|---|---|
| Bridge map (TIL data) | `data/library/evolution.til:80-120` | 22 conversions defined across 14 types | Already exists |
| Bridge map (C++ integration) | — | **Not implemented** | Load TIL map into C++ structure accessible by `SignatureIndex` |
| `find_type_compatible()` | `signature_index.hpp` | **Does not exist**. `find_exact()` has comment: "Type-level matching is a future enhancement" | New method filtering candidates by input types |
| Stack simulator | `stack_simulator.hpp/cpp` | Tracks output types in `SimState::type_stack`. Input types are counted but not typed. | Enhance to track input types at mutation points |
| `substitute_call()` | `ast_genetic_ops.cpp:38-114` | Uses `find_restricted()` (pool) or `find_tiered()` (tags). No type filtering. | Use `find_type_compatible()` |
| `grow_node()` | `ast_genetic_ops.cpp:301-382` | Inserts random `(1,1)` words. 70% stack-neutral, 30% literals. No type awareness. | Query stack types, select type-legal words |
| Type repair | `type_repair.hpp/cpp` | Inserts shuffles only (`swap`, `rot`, `roll`). Cannot insert bridge words. Cannot create missing types — returns false if type not on stack. | Enhance to insert bridge words from bridge map |
| Cycle detection | — | **Not implemented** | Option B: adjacent-inverse bridge check (per Bridges design recommendation) |
| Bridge logging | `evolve_logger.hpp:37` | Category defined, marked "future" | Wire into mutation logging |

### Type Repair — Confirmed Non-Functional (v1.9.3)

The diff logging R column (repair marker) has been blank across all experiments. Investigation confirmed (documented at bottom of Bridges design doc):

1. **Math pool**: All words consume/produce `Integer`. No type mismatches possible.
2. **Full dictionary**: Most words have `Unknown` type signatures. `type_repair.cpp:99` — `actual == T::Unknown` → `continue`. Mismatches are invisible.
3. **Stack simulator starts empty**: First N words' inputs checked against empty stack → underflow → `continue`.

The R column is infrastructure waiting for type-directed bridges to provide concrete type information.

### Modular Co-Evolution — Infrastructure Audit

| Component | File(s) | Current State | MCE Needed |
|---|---|---|---|
| Dictionary multi-impl | `dictionary.hpp/cpp` | Multiple impls per word concept. Fully functional. | Already exists |
| `WeightedRandom` selection | `selection/` | Picks highest-weight impl at each call site. Fully functional. | Already exists |
| `evolve-word` | `evolution_engine.cpp` | Evolves one concept's population per call. | Already exists |
| Proxy fitness | — | **Not implemented**. `evolve-word` evaluates the word being evolved, not a chain word. | New: evolve word A but evaluate fitness through chain word B |
| Round-robin orchestration | — | **Not implemented** at engine level (achievable via TIL loop). | TIL-level or new engine method |
| Credit assignment | — | **Not implemented**. Implicit credit (chain fitness → sub-concept weight) requires proxy fitness. | Depends on proxy fitness mechanism |
| `EvolutionConfig` | `evolution_engine.hpp:29-46` | No team structure, subpopulation tracking, or inter-word dependency fields. | Minimal changes for Phase 1 (proxy fitness only) |

### SigType Coverage

The `TypeSignature::Type` enum (`word_impl.hpp:21-36`) defines 14 types: Unknown, Integer, Float, Boolean, String, Array, ByteArray, Map, Json, Matrix, Observable, Xt, DataRef, Custom. The bridge map covers conversions between 10 of these (excluding Unknown, Observable, Xt, DataRef, Custom).

---

## Interaction Effects

### TDB Alone

Eliminates the crash floor. Every mutation produces a runnable program. Distance fitness gradient is always visible. Selection pressure is continuous.

**Remaining weakness:** Population still converges by Gen 6. All 10 impls become near-identical. Every mutation is a lateral move. The gradient exists but the search space is explored from a single point.

### MCE Alone

Creates 1000 effective combinations from 30 sub-concept modules (10 impls x 3 concepts). Module preservation protects working sub-components. Natural crossover via module swapping.

**Remaining weakness:** 90% of sub-concept impls crash individually. The combinatorial space is vast but mostly dead. Credit assignment is noisy when most evaluations score 0.1.

### TDB + MCE

1000 combinations on a smooth gradient. Each sub-concept mutation is type-safe → all combinations are runnable → distance fitness distinguishes every combination → selection pressure operates across the full combinatorial space.

```
TDB alone:    Smooth gradient, single-point convergence
MCE alone:    Vast combinatorial space, 90% dead programs
TDB + MCE:    Smooth gradient across a vast combinatorial space
              → continuous selection pressure with exponential exploration
```

This is the target architecture. The question is implementation order.

---

## Recommendation: TDB First

### Rationale

1. **TDB is the prerequisite.** MCE without TDB searches a crash floor. TDB without MCE at least has a visible gradient. The Gen 6 convergence stall may be partially mitigated when mutations stop crashing, since the population has continuous selection pressure instead of 90% noise.

2. **TDB is fully automatic; MCE Phase 1 requires manual decomposition.** TDB improves every evolution task without user intervention. MCE Phase 1 requires the user to manually decompose `f(x) = x^2 + 3x + 5` into `square-term`, `linear-term`, `offset` — which presupposes knowing the answer structure.

3. **TDB has a detailed implementation spec.** The Bridges design specifies exactly which methods to add to `SignatureIndex`, how `substitute_call()` and `grow_node()` change, how type repair is enhanced, and which cycle detection strategy to use (Option B). MCE's proxy-fitness mechanism and credit assignment need additional design work.

4. **TDB activates dormant infrastructure.** The type repair R column, the Bridge logging category, and the stack simulator's type tracking are all waiting for TDB. Implementing TDB brings these existing components online.

5. **MCE becomes much more powerful after TDB.** With type-safe mutations, MCE's combinatorial amplification operates on a smooth gradient. The synergy is multiplicative: TDB provides the gradient, MCE provides the exploration space.

### Counter-Argument Considered

MCE Phase 1 is conceptually simpler and could be tested with math-only pools (where crashes don't happen) to validate the modular approach before investing in TDB. However, math-only pools create isolation — the original problem that TDB was designed to solve. Validating MCE in an artificially crash-free environment doesn't prove it works in the general case.

---

## Proposed Implementation Sequence

### Phase 1 — TDB Core (Highest Impact)

Target: Eliminate the crash floor for substitute and grow mutations.

1. Load bridge map from TIL into C++ data structure accessible by `SignatureIndex`
2. Add `find_type_compatible()` to `SignatureIndex` — filter candidates by input types
3. Enhance stack simulator to track input types at mutation points (not just output types)
4. Modify `substitute_call()` to use `find_type_compatible()` instead of `find_compatible()`
5. Modify `grow_node()` to select type-legal words (bridge words appear naturally as candidates)
6. Implement adjacent-inverse cycle detection (Option B from Bridges design)
7. Wire bridge insertions into the existing Bridge logging category

**Validation:** Run the symbolic regression experiment (`f(x) = x^2 + 3x + 5`) with full dictionary (no pool restriction). Success criteria: mutation crash rate < 10%, fitness shows upward trend past Gen 6.

### Phase 2 — TDB Type Repair Enhancement

Target: Type repair becomes constructive (inserts bridge words, not just shuffles).

8. Enhance `type_repair` to consult bridge map when a type mismatch is found
9. Insert bridge words to convert stack types, not just shuffle existing values
10. Validate R column populates in diff logging

**Validation:** R column shows bridge insertions in evolution diff logs. Programs that would have crashed now run with bridge-repaired type conversions.

### Phase 3 — MCE Orchestration (Builds on Working TDB)

Target: Modular evolution with combinatorial population amplification.

11. Add proxy-fitness mechanism: `evolve-word` evaluates fitness through a chain word
12. TIL-level round-robin orchestration (`evolve-chain` word)
13. Implicit credit via existing weight system (no new code for Phase 1 credit assignment)

**Validation:** Decomposed symbolic regression (`square-term` + `linear-term` + `offset`) shows independent module evolution with preserved working sub-components. Effective population > monolithic.

### Phase 4 — MCE Enhancements (Future)

14. Proportional evolution budget (more mutations for underperforming sub-concepts)
15. Co-evolutionary credit assignment (if implicit credit shows oscillation)
16. Evolved chain structure (Phase 2 of MCE design — chain topology itself evolves)

---

## Summary

Type-Directed Bridges and Modular Co-Evolution solve complementary problems at different levels of the evolution pipeline. TDB fixes the mutation level (type-safe substitutions), MCE fixes the structural level (modular decomposition). TDB is the foundation — it makes the distance fitness gradient visible, activates dormant infrastructure, and works automatically on any evolution task. MCE is the amplifier — it expands the effective search space exponentially and preserves working sub-components. The recommended order is TDB first, MCE second, with each phase validated against the symbolic regression benchmark before proceeding.
