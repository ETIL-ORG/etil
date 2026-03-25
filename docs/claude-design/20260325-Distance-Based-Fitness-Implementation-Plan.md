# Distance-Based Fitness — Implementation Plan

**Date:** 2026-03-25
**Design Doc:** `20260325-Distance-Based-Fitness-Design.md`
**Prerequisites:** v1.7.5 (AST evolution improvements)
**Status:** Planned

---

## Phase 0: Core Distance Functions and Fitness Mode

**Goal:** Add distance-based scoring to the fitness evaluator. Backward-compatible — binary mode remains the default.

**New files:** None

**Modified files:**
- `include/etil/evolution/fitness.hpp` — Add `FitnessMode`, `value_distance()`, extend `FitnessResult`
- `src/evolution/fitness.cpp` — Implement distance functions, add distance-mode evaluation path
- `include/etil/evolution/evolution_engine.hpp` — Add `fitness_mode` and `distance_alpha` to `EvolutionConfig` and `WordEvolution`
- `src/evolution/evolution_engine.cpp` — Pass fitness mode to evaluator
- `src/evolution/evolve_logger.cpp` — Log distance scores

**Steps:**

1. Add `FitnessMode` enum (`Binary`, `Distance`) to `fitness.hpp`
2. Add `mean_distance` field to `FitnessResult`
3. Add `value_distance(const Value& actual, const Value& expected) → double`:
   - Integer/Float: `|a - e|` with cross-type promotion
   - Boolean: 0.0 or 1.0
   - Type mismatch: 1000.0
4. Add `run_single_test_distance()` — like `run_single_test()` but returns distance instead of bool. Handles stack depth mismatch (100.0 per missing/extra), execution failure (infinity)
5. Add distance-mode path in `evaluate()` — per-test score = `1.0 / (1.0 + alpha * distance)`, mean across all tests
6. Add `fitness_mode` (default `Binary`) and `distance_alpha` (default 1.0) to `EvolutionConfig`
7. Add `fitness_mode` to `WordEvolution` so per-word override is possible
8. Pass fitness mode through `evolve_word()` to `evaluate()`
9. Log distance scores at granular level in evolution engine
10. Build debug + release, run all tests (binary mode unchanged, zero regressions)

**Estimated effort:** Solo 1 day / AI-assisted 2 hours

---

## Phase 1: TIL Interface and Validation

**Goal:** Expose distance fitness to TIL. Re-run experiments.

**Modified files:**
- `src/core/primitives.cpp` — Register `evolve-fitness-mode` and `evolve-fitness-alpha`
- `tests/unit/test_primitives.cpp` — Update concept count

**Steps:**

1. Register `evolve-fitness-mode ( n -- )` — 0=binary, 1=distance. Sets on `EvolutionConfig`
2. Register `evolve-fitness-alpha ( f -- )` — sets scaling factor
3. Update test concept count
4. Build and test
5. Re-run symbolic regression with distance mode + math pool + logging
6. Re-run function synthesis with distance mode
7. Verify sorting network unchanged (uses its own fitness)
8. Analyze logs: verify distance scores provide gradient (mean_distance decreasing over generations)
9. Commit results

**Estimated effort:** Solo 3 hours / AI-assisted 1 hour

---

## Total Estimated Effort

| Phase | Solo Human | AI-Assisted |
|---|---|---|
| Phase 0: Core distance functions | 1 day | 2 hours |
| Phase 1: TIL interface + validation | 3 hours | 1 hour |
| **Total** | **~1.5 days** | **~3 hours** |

---

## Success Criteria

1. **Binary mode unchanged**: All 1361 existing tests pass with zero regressions
2. **Distance gradient**: Symbolic regression seed (`dup +`) scores >0.05 (was 0.0 in binary mode)
3. **Fitness improves over generations**: Mean distance decreases over 100+ generations with math pool
4. **Partial credit**: Function synthesis children with 2/5 binary passes score higher than 1/5 children in distance mode
5. **Logging**: Distance scores visible in evolve log at granular level
