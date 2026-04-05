# Type Bridge Back Propagation (TBBP) — Implementation Plan

**Date:** 2026-04-05
**References:** `20260403B-Type-Directed-Bridges-Implementation-Plan.md` (TDB, completed v1.12.10), `20260405A-MCE-Design-Review.md` (sequencing rationale)
**Prerequisites:** v1.12.11 (TDB complete, BridgeMap operational, TypeRepair inserting single/multi-hop bridges)
**Status:** Plan
**Workflow:** See `docs/claude-knowledge/20260403A-feature-branch-workflow.md`

---

## Overview

TDB's bridge system treats all bridges as equally likely candidates. A `Matrix → Float` lookup might return `mat-norm`, `mat-trace`, `mat-mean`, `mat-sum` — currently the repair picks the first one found by BFS, not the most productive one for the current problem.

**TBBP adds a learned weight to each `BridgeEdge`.** Bridges that led to child-fitness improvements get reinforced; bridges that led to crashes or regressions get attenuated. Over the course of a session, the weights encode empirical bridge preferences for the current problem domain.

**Why now:** the `20260405A-MCE-Design-Review.md` analysis concluded that TBBP should precede MCE to (1) establish a baseline before combinatorial amplification, (2) decouple TBBP's gains from MCE's gains during measurement, (3) validate the bridge map's signal in isolation, and (4) deliver a small, well-bounded increment before the larger MCE effort.

**Starting version:** v1.12.11 (master)
**Feature branch:** Created via `scripts/branch.sh tbbp-adaptive-weights` → v1.13.0
**Each phase:** `version-bump.sh patch` → implement → build → test → commit
**Final phase:** Update docs (README.md), merge to master, tag

---

## Design

### Weighted BridgeEdge

Each edge carries a weight and usage counters:

```cpp
struct BridgeEdge {
    core::TypeSignature::Type from;
    core::TypeSignature::Type to;
    std::string word;

    // TBBP state (per-session, reset on BridgeMap construction)
    double weight = 1.0;         // current EMA weight
    uint64_t selections = 0;     // total times selected
    uint64_t successes = 0;      // selections that led to child > parent
};
```

### Weighted Bridge Selection

`find_path()` currently returns the first path found by BFS. The TBBP version selects among candidate paths using weights:

```cpp
// Existing (TDB):
std::vector<std::string> find_path(T from, T to, size_t max_hops = 2) const;

// New (TBBP):
std::vector<std::string> select_path(T from, T to, size_t max_hops = 2);
// Uses weighted-random among available bridge paths.
// Records which edges were selected (call begin_mutation first).
```

When multiple bridges exist for the same type pair (e.g., `Matrix → Float` has `mat-norm`, `mat-trace`, `mat-mean`, `mat-sum`), `select_path()` picks via weighted-random proportional to edge weights.

For multi-hop paths, the weight of a path = product of its edge weights. The selection is over whole paths, not individual edges.

### Per-Mutation Usage Tracking

The BridgeMap tracks which edges were selected during a mutation, so weight updates can be applied after fitness evaluation:

```cpp
class BridgeMap {
public:
    // Begin recording bridge usages for the current mutation
    void begin_mutation();

    // Record that an edge was selected (called internally by select_path)
    // Public so other callers can record usages manually if needed
    void record_usage(T from, T to, const std::string& word);

    // Apply EMA update to all edges recorded since begin_mutation
    // reward = 1.0 if child fitness improved over parent, 0.0 otherwise
    void end_mutation(double reward);
};
```

**Flow:**
1. `evolution_engine` calls `bridge_map_.begin_mutation()` before `ast_genetic_ops_.mutate()`
2. During mutation, `TypeRepair` calls `bridge_map_->select_path()` which records usages internally
3. `evolution_engine` computes child fitness
4. `evolution_engine` calls `bridge_map_.end_mutation(reward)` with reward computed from parent_fitness vs child_fitness

### EMA Update Rule

```
reward = 1.0 if child_fitness > parent_fitness else 0.0
new_weight = (1 - α) * old_weight + α * reward
```

With a **floor** to preserve exploration:
```
new_weight = max(new_weight, MIN_WEIGHT)
```

**Parameters:**
- α (learning rate) = 0.1
- MIN_WEIGHT = 0.05 (ensures underperforming bridges are still tried occasionally)
- Initial weight = 1.0 (uniform at start)

These are reasonable defaults drawn from multi-armed bandit literature. They should be configurable via `EvolutionConfig` for tuning.

### Runtime Toggle: `tbbp_enabled`

TBBP is gated by a runtime flag so the full ablation matrix can be run from a single binary:

| `tbbp_enabled` | MCE | What it measures |
|---|---|---|
| false | off | Baseline: TDB only, uniform bridge selection |
| **true** | off | TBBP contribution: adaptive bridges on monolithic evolution |
| false | on | MCE contribution: combinatorial amplification without bridge learning |
| true | on | Combined: full system |

When `tbbp_enabled == false`:
- `select_path()` falls through to `find_path()` behavior (first path by BFS, no weighting)
- `record_usage()` is a no-op
- `end_mutation()` is a no-op
- Bridge weights remain at 1.0 (never updated)

When `tbbp_enabled == true` (default):
- Full TBBP semantics as described above

**Implementation:** a single `bool tbbp_enabled_ = true` on `BridgeMap`, checked at the top of `select_path()` / `record_usage()` / `end_mutation()`. The flag is set via `BridgeMap::set_tbbp_enabled(bool)`, wired from `EvolutionConfig::tbbp_enabled` (default `true`) at engine construction, and exposed to TIL via `evolve-tbbp-enabled?` ( bool -- ).

**Why a flag, not separate branches:** a flag delivers the same 4-configuration measurement matrix from a single build with no merge conflicts, no rebase tax, no "combine" phase. It also persists as a user-facing diagnostic tool post-v1.13, letting researchers disable TBBP on their own problems for ablation studies without rebuilding.

### Logging

Extend the existing `EvolveLogCategory::Bridge` category:

```
[Bridge] select: Matrix→Float chose 'mat-norm' (w=0.84, 3/5 selections successful)
[Bridge] update: 'mat-norm' 0.84 → 0.86 (reward=1.0, child=0.42 > parent=0.38)
[Bridge] update: 'mat-trace' 0.52 → 0.47 (reward=0.0, child=0.31 < parent=0.38)
[Bridge] summary: top bridges: int->float(0.92) array-length(0.87) slength(0.84) ...
```

A summary is logged at the end of each generation.

---

## Phases

### Phase 0 — Weighted BridgeEdge Infrastructure + Runtime Toggle

**Start:** Create feature branch via `scripts/branch.sh tbbp-adaptive-weights` → v1.13.0

**Goal:** Extend `BridgeEdge` with weight and counters, add `tbbp_enabled` toggle to `BridgeMap`. Preserve existing API.

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/bridge_map.hpp` | Add `weight`, `selections`, `successes` to `BridgeEdge`; add `tbbp_enabled_` + setter to `BridgeMap` |
| `src/evolution/bridge_map.cpp` | Initialize fields in `BridgeMap::add()` |
| `tests/unit/test_bridge_map.cpp` | Tests: weight defaults to 1.0, counters to 0, `tbbp_enabled_` defaults to true |

**BridgeMap changes:**
```cpp
class BridgeMap {
public:
    void set_tbbp_enabled(bool e) { tbbp_enabled_ = e; }
    bool tbbp_enabled() const { return tbbp_enabled_; }
private:
    bool tbbp_enabled_ = true;  // default on
};
```

**No API changes for existing callers.** `find_path()`, `find_bridge()`, `conversions_from()` work unchanged. This phase only adds state.

**Unit tests:**
- New edge has weight 1.0, selections 0, successes 0
- BridgeMap `tbbp_enabled()` defaults to true
- `set_tbbp_enabled(false)` / `tbbp_enabled()` round-trips
- `find_path()` still returns same paths as before (weights don't affect BFS yet)

**Commit:** `v1.13.1 Phase 0 — BridgeEdge weight state and tbbp_enabled toggle`

---

### Phase 1 — Weighted Bridge Selection

**Start:** `scripts/version-bump.sh patch` → v1.13.2

**Goal:** Add `select_path()` method that picks among available bridge paths via weighted-random.

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/bridge_map.hpp` | Add `select_path()`, keep `find_path()` |
| `src/evolution/bridge_map.cpp` | Implement weighted-random path selection |
| `tests/unit/test_bridge_map.cpp` | Tests: weighted selection distribution, RNG injection |

**Design:**
- **If `tbbp_enabled_ == false`: fall through to `find_path()` (uniform BFS behavior, no state mutation).**
- Otherwise:
  - Enumerate all paths from → to within max_hops (there may be several)
  - For single-hop: if multiple edges exist for the same pair, weight-by-edge-weight
  - For multi-hop: weight-by-product-of-edge-weights
  - Use `std::discrete_distribution` with the weight vector
  - RNG is injected (either shared from EvolutionEngine or owned by BridgeMap)

**Unit tests:**
- With all weights equal → roughly uniform selection distribution (chi-squared test)
- With one bridge weight=0.9, others=0.1 → dominant selection of the heavy edge
- Empty path list → returns empty vector
- Single path → always selected regardless of weight
- **With `tbbp_enabled=false`: `select_path()` returns identical results to `find_path()` (deterministic BFS)**

**Commit:** `v1.13.2 Phase 1 — Weighted bridge selection API`

---

### Phase 2 — Per-Mutation Usage Tracking

**Start:** `scripts/version-bump.sh patch` → v1.13.3

**Goal:** Add `begin_mutation()`, `record_usage()`, `end_mutation(reward)` to track which bridges were used per mutation and apply EMA updates.

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/bridge_map.hpp` | Add mutation-tracking API, EMA parameters |
| `src/evolution/bridge_map.cpp` | Implement usage recording and EMA update |
| `tests/unit/test_bridge_map.cpp` | Tests: usage tracking, EMA update, floor enforcement |

**Implementation:**
```cpp
class BridgeMap {
private:
    // Per-mutation state
    struct EdgeRef { T from; T to; std::string word; };
    std::vector<EdgeRef> current_mutation_usages_;

    // EMA parameters (configurable)
    double alpha_ = 0.1;
    double min_weight_ = 0.05;

public:
    void set_alpha(double a) { alpha_ = a; }
    void set_min_weight(double m) { min_weight_ = m; }

    void begin_mutation() {
        if (!tbbp_enabled_) return;
        current_mutation_usages_.clear();
    }

    void record_usage(T from, T to, const std::string& word) {
        if (!tbbp_enabled_) return;
        current_mutation_usages_.push_back({from, to, word});
        auto* edge = find_edge_mut(from, to, word);
        if (edge) edge->selections++;
    }

    void end_mutation(double reward) {
        if (!tbbp_enabled_) return;
        for (const auto& ref : current_mutation_usages_) {
            auto* edge = find_edge_mut(ref.from, ref.to, ref.word);
            if (!edge) continue;
            edge->weight = std::max(
                (1.0 - alpha_) * edge->weight + alpha_ * reward,
                min_weight_
            );
            if (reward > 0.5) edge->successes++;
        }
        current_mutation_usages_.clear();
    }
};
```

**Also update `select_path()` to call `record_usage()` for each edge in the chosen path.**

**Unit tests:**
- `begin_mutation` / `end_mutation(1.0)` raises weights of used edges
- `end_mutation(0.0)` lowers weights of used edges but not below `min_weight`
- `end_mutation` without `begin_mutation` is a no-op (safe)
- Nested `begin_mutation` calls are flagged (or cleared-and-warned)
- Selection counters increment per `select_path()` call
- **With `tbbp_enabled=false`: `begin_mutation`, `record_usage`, `end_mutation` all return immediately with no state change**
- **With `tbbp_enabled=false`: weights remain at 1.0 after many mutations**

**Commit:** `v1.13.3 Phase 2 — Mutation usage tracking and EMA weight updates`

---

### Phase 3 — Wire Into TypeRepair

**Start:** `scripts/version-bump.sh patch` → v1.13.4

**Goal:** `TypeRepair` now uses `select_path()` instead of `find_path()`, so bridge usages are recorded automatically.

**Files:**
| File | Change |
|---|---|
| `src/evolution/type_repair.cpp` | Change `find_path()` call to `select_path()` |
| `tests/unit/test_type_repair.cpp` | Tests: bridge selection recorded, weights affect repair choice |

**The actual change is one line** in `repair_sequence()`:
```cpp
// Before:
auto path = bridge_map_->find_path(actual, needed, 2);

// After:
auto path = bridge_map_->select_path(actual, needed, 2);
```

**But we also need `BridgeMap` to be non-const in `TypeRepair`** since `select_path()` mutates state (records usages, increments counters). Update the member type:
```cpp
// Before:
const BridgeMap* bridge_map_ = nullptr;

// After:
BridgeMap* bridge_map_ = nullptr;
```

And update `set_bridge_map()` signature.

**Unit tests:**
- After `TypeRepair::repair()` inserts a bridge, `bridge_map` records the usage
- When one bridge has weight 10× another, repair picks the heavy one ≥80% of the time
- Weight floor prevents starvation: low-weight bridges still selected occasionally

**Commit:** `v1.13.4 Phase 3 — TypeRepair uses weighted bridge selection`

---

### Phase 4 — Wire Into EvolutionEngine + Config Flag

**Start:** `scripts/version-bump.sh patch` → v1.13.5

**Goal:** Evolution engine calls `begin_mutation()` / `end_mutation(reward)` around each child mutation. Wire `EvolutionConfig::tbbp_enabled` to `BridgeMap` and expose via TIL word.

**Files:**
| File | Change |
|---|---|
| `include/etil/evolution/evolution_engine.hpp` | Add `bool tbbp_enabled = true` to `EvolutionConfig` |
| `src/evolution/evolution_engine.cpp` | Wrap mutation with begin/end_mutation, compute reward, call `bridge_map_.set_tbbp_enabled(config.tbbp_enabled)` at construction |
| `src/core/primitives.cpp` | Add `evolve-tbbp-enabled?` TIL word |
| `tests/unit/test_evolution.cpp` (or similar) | Tests: bridge weights change when enabled, stay at 1.0 when disabled |

**Implementation in `evolve_word()`:**
```cpp
for (size_t i = 0; i < config_.generation_size; ++i) {
    WordImplPtr child;
    double parent_fitness = 0.0;

    if (coin(local_rng) < config_.mutation_rate || evolvable.size() < 2) {
        // Mutation path
        auto* parent = parent_selector_.select(evolvable);
        if (!parent) continue;

        // Look up parent's baseline fitness from results[]
        for (const auto& [impl, fr] : results) {
            if (impl.get() == parent) { parent_fitness = fr.fitness; break; }
        }

        bridge_map_.begin_mutation();
        child = ast_genetic_ops_.mutate(*parent);
        if (!child) {
            bridge_map_.end_mutation(0.0);  // mutation failed, punish any bridges
            continue;
        }
    } else {
        // ... crossover path (similar) ...
    }

    auto fr = fitness_.evaluate(*child, tests, dict_, ...);

    // TBBP reward signal
    double reward = (fr.fitness > parent_fitness) ? 1.0 : 0.0;
    bridge_map_.end_mutation(reward);

    // ... existing weight/prune logic ...
}
```

**TIL word:**
```
evolve-tbbp-enabled?  ( flag -- )
```
Turns TBBP on or off for the current evolution engine. Takes effect on subsequent mutations; already-accumulated weights persist.

**Unit tests:**
- After N generations with one "productive" bridge (returns correct result), that bridge's weight > 0.5
- After N generations with one "unproductive" bridge (always crashes), that bridge's weight approaches floor (0.05)
- Mutation failures count as reward=0
- **With `config.tbbp_enabled=false`: weights remain at 1.0 after full evolution cycle**
- **TIL: `false evolve-tbbp-enabled?` disables at runtime**
- **TIL: `true evolve-tbbp-enabled?` re-enables at runtime**

**Commit:** `v1.13.5 Phase 4 — Evolution engine drives TBBP weight updates + config flag`

---

### Phase 5 — Bridge Weight Logging

**Start:** `scripts/version-bump.sh patch` → v1.13.6

**Goal:** Log bridge selection decisions and weight updates via `EvolveLogCategory::Bridge`.

**Files:**
| File | Change |
|---|---|
| `src/evolution/bridge_map.cpp` | Log selection and update events if logger attached |
| `include/etil/evolution/bridge_map.hpp` | Add `set_logger()` |
| `src/evolution/evolution_engine.cpp` | Log generation-end bridge weight summary |

**Log output examples:**
```
[Bridge] select: Matrix→Float chose 'mat-norm' (w=0.84, 3/5=60% success)
[Bridge] update: 'mat-norm' 0.84 → 0.86 (reward=1.0)
[Bridge] update: 'mat-trace' 0.52 → 0.47 (reward=0.0)
[Bridge] generation summary: top 5 bridges by weight:
  int->float (w=0.92, 45 sel, 42 succ, 93% rate)
  array-length (w=0.87, 12 sel, 10 succ, 83% rate)
  slength (w=0.84, 8 sel, 6 succ, 75% rate)
  sjoin (w=0.41, 3 sel, 1 succ, 33% rate)
  float->int (w=0.15, 2 sel, 0 succ, 0% rate)
```

**Unit tests:**
- Log lines produced when Bridge category enabled
- No log output when Bridge category disabled (zero overhead)

**Commit:** `v1.13.6 Phase 5 — Bridge weight logging`

---

### Phase 6 — Integration Validation

**Start:** `scripts/version-bump.sh patch` → v1.13.7

**Goal:** Run the quadratic regression benchmark with TBBP enabled. Compare metrics against the Phase 9 TDB baseline (`20260404T080925-evolve.log`).

**Files:**
| File | Change |
|---|---|
| `tests/til/test_tbbp_validation.til` | New: integration test with bridge weight assertions |
| `tests/til/test_tbbp_validation.sh` | New: validation wrapper |

**Success criteria:**
- Evolution runs without crashing (same as TDB baseline)
- Bridge weights spread: after 50 generations, top bridge has weight ≥ 0.7, bottom bridge has weight ≤ 0.3 (indicates selection pressure is working)
- Dominant bridges for quadratic regression should be numeric (`int->float`, `float->int` if they're used) — domain-appropriate
- No bridge hits floor (0.05) in first 5 generations (warm-up period)
- Log summary shows sensible weight distribution

**Benchmark comparison using the runtime flag:**

The `evolve-tbbp-enabled?` flag lets a single TIL session measure both configurations back-to-back with the same RNG seed:

```til
# Baseline run: TBBP off
false evolve-tbbp-enabled?
42 random-seed
<register tests, run N generations, record metrics>

# Treatment run: TBBP on
true evolve-tbbp-enabled?
42 random-seed
<register tests, run N generations, record metrics>
```

Metrics collected per configuration:
- Generations to reach fitness > 0.9
- Mean fitness improvement per generation (post-warmup)
- Fraction of children that exceed parent fitness
- Final bridge weight distribution (TBBP-on only)

If TBBP shows improvement on any of these, it's working. If all metrics are within noise, the bridge map may not be adding signal for this problem (interesting finding either way). Either outcome is publishable.

**Commit:** `v1.13.7 Phase 6 — Integration validation for TBBP`

---

### Phase 7 — Documentation and Merge

**Goal:** Update docs, merge feature branch to master.

**Steps:**

1. Update `README.md`:
   - Add TBBP to evolution engine features
   - Document `evolve-tbbp-enabled?` ( flag -- ) TIL word and its role in ablation studies
   - Document `evolve-alpha` and `evolve-min-bridge-weight` TIL configuration words (added in Phase 4)
   - Note the EMA update rule and floor

2. Update `data/help.til`:
   - Add help entries for `evolve-tbbp-enabled?` and other TBBP config words

3. Update `CLAUDE.md`:
   - Mention TBBP in the evolution section alongside TDB

4. Commit documentation updates

5. **Ask permission**, then run `super-push.sh` targeting the CI server

6. Wait for CI pipeline to pass

7. **Ask permission**, then run `scripts/github-push.sh`

---

## Version Summary

| Phase | Version | Description |
|---|---|---|
| — | v1.12.11 | Master before feature branch |
| branch | v1.13.0 | Feature branch created (minor bump) |
| 0 | v1.13.1 | Weighted BridgeEdge infrastructure |
| 1 | v1.13.2 | Weighted bridge selection (`select_path`) |
| 2 | v1.13.3 | Per-mutation tracking and EMA update |
| 3 | v1.13.4 | TypeRepair uses weighted selection |
| 4 | v1.13.5 | Evolution engine drives weight updates |
| 5 | v1.13.6 | Bridge weight logging |
| 6 | v1.13.7 | Integration validation |
| 7 | v1.13.8+ | Documentation, merge, tag |

---

## Design Decisions

### Why EMA, not a multi-armed bandit?

MAB algorithms (UCB1, Thompson sampling) are more sophisticated and handle exploration/exploitation better. But:
- ETIL's `selection/` already has bandit strategies for word selection — adding them here duplicates machinery
- EMA + weight floor gives a simple, interpretable update rule
- If EMA proves insufficient (e.g., gets stuck on an early winner), a MAB variant can replace it with minimal API changes

Start simple. Upgrade only if measurements show need.

### Why record usages per-mutation instead of per-child?

A single mutation may insert multiple bridges (multi-hop repair). They all contributed to the child's success or failure, so they should all receive the same reward. Per-mutation is the natural granularity.

### Why not update weights on every bridge query?

Non-terminal queries (e.g., `has_conversions()`, `conversions_from()`) don't select anything — they're pure inspection. Only `select_path()` represents "bridge X was chosen for use." Update weights at the point of commitment.

### What about bridges used during `grow_node()` and `substitute_call()`?

Currently, bridges appear as regular candidates in `find_type_compatible()` results. They're not specially tracked. TBBP in this plan only updates weights for bridges inserted via `TypeRepair`.

Future enhancement: track when a bridge word is selected in grow/substitute and record usage there too. This is orthogonal and can be added post-v1.13.

### Parent fitness lookup

The evolution engine evaluates parents at the start of each generation (`evolution_engine.cpp:92-103`). The `results` vector holds parent `FitnessResult`s. TBBP reuses this data — no new fitness evaluations needed.

---

## Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Weights converge too fast (α=0.1 too aggressive) | EvolutionConfig exposes α; benchmark with α=0.05 and α=0.2 |
| Weight floor (0.05) too low — bridges starve | Benchmark shows exploration rate; adjust if needed |
| Weight floor too high — no differentiation | Same |
| "Used multiple bridges, only one was actually helpful" — credit misattribution | Multi-hop bridges are rare; single-hop dominates. If metrics show oscillation, add positional weighting |
| Bridges not actually used in the quadratic benchmark | Diagnostic finding; may indicate bridge map needs non-numeric test problems to be useful |
| Per-session reset means no long-term learning | Matches TDB's design philosophy (per-session is correct for evolution). Persistence is a separate future path |

---

## Future Paths (Post-TBBP)

### Grow/Substitute Bridge Tracking

Extend usage tracking to `grow_node()` and `substitute_call()` when a bridge word is selected as a candidate. Current plan only covers repair-level bridge insertion.

### UCB1 Bridge Selection

Replace EMA + floor with UCB1:
```
score = weight + c * sqrt(ln(total_selections) / bridge_selections)
```
Provides exploration bonus for under-sampled bridges. Reuses machinery from `selection/`.

### Thompson Sampling

Treat each bridge's reward as a Bernoulli random variable. Sample from each bridge's posterior per selection. More statistically rigorous than UCB1, similar complexity.

### Cross-Session Persistence

Save bridge weights to disk at end of run, load at start of next run with the same problem signature. Requires careful invalidation when test cases or target function change.

### Bridge Weight Export

TIL word to query current bridge weights: `s" Matrix" s" Float" bridge-weights` → array of (word, weight) pairs. Useful for diagnostics and manual tuning.

---

## Success Definition

TBBP is successful if:

1. **Technical correctness:** All unit tests pass, no regressions, log summaries show sensible weight distributions.

2. **Signal detection:** Bridge weights spread (not all stuck at 1.0 or floor) on benchmark problems.

3. **Baseline improvement:** On the quadratic regression benchmark, TBBP either improves time-to-fitness or reveals that bridges aren't the bottleneck (both are useful findings).

4. **MCE foundation:** TBBP is stable enough that MCE Phase 1a can build on it without worrying about bridge selection quality.

Total estimated effort: **~1-2 days of focused work** across 7 phases. The code surface is small (primarily `bridge_map.{hpp,cpp}` and a handful of integration points). The largest effort is in writing and validating the integration tests.
