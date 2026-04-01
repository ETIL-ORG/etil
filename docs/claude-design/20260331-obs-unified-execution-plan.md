# Observable Unified Execution — Implementation Plan

**Date:** 2026-03-31
**Design Doc:** `20260331-obs-unified-execution-design.md`
**Status:** Planned

---

## Overview

Replace the fragmented sync/async observable execution with a single `execute_pipeline()` function. Eliminates 27 duplicated operator cases (~500 lines), removes `needs_async()`, and enables dynamic node creation for FlatMap/SwitchMap/RetryDelay.

6 phases, each independently testable and committable. The existing `execute_observable()` and `build_async_pipeline()` shrink incrementally as operators are migrated. Deleted at the end.

---

## Operator Inventory

### 27 operators currently in BOTH functions (duplicated — to be unified)

**Transforms (11):** Map, MapWith, Filter, FilterWith, Scan, Take, Skip, Tap, StartWith, Timestamp, TimeInterval

**Temporal transforms (10):** Delay, DelayEach, DebounceTime, ThrottleTime, SampleTime, AuditTime, BufferTime, TakeUntilTime, Timeout, RetryDelay

**Sources (1):** Timer

**Combination (3):** Merge, Concat, Zip

**Incomplete async (2):** FlatMap, SwitchMap (fall through to sync in build_async)

### 24 operators currently in execute_observable ONLY (sync-only)

**Sources (5):** FromArray, Of, Empty, Range, Reduce (terminal-only)

**Transforms (10):** Distinct, DistinctUntil, Pairwise, First, Last, TakeWhile, Buffer, BufferWhen, Window, Finalize

**Combination (1):** Catch

**File I/O (5):** ReadBytes, ReadLines, ReadJson, ReadCsv, ReadDir

**HTTP (3):** HttpGet, HttpPost, HttpSse

---

## Phase 1: Scaffold `execute_pipeline()` and Route Terminal Operators

**Goal:** Create the new function, route all terminal operators through it, zero behavior change.

### Step 1.1: Create `execute_pipeline()`

Add the new function signature:

```cpp
static bool execute_pipeline(HeapObservable* obs, ExecutionContext& ctx,
                              const Observer& observer,
                              AsyncPipeline* pipeline = nullptr);
```

Initial implementation: delegates to the existing functions.

```cpp
static bool execute_pipeline(HeapObservable* obs, ExecutionContext& ctx,
                              const Observer& observer,
                              AsyncPipeline* pipeline) {
    if (pipeline) {
        build_async_pipeline(obs, ctx, observer, *pipeline);
        return true;  // async — nodes registered, not executed yet
    }
    return execute_observable(obs, ctx, observer);
}
```

### Step 1.2: Route Terminal Operators

Replace `run_observable()` calls in all 7 terminal operators with a unified pattern:

```cpp
// Before:
bool ok = run_observable(src, ctx, observer_lambda);

// After:
AsyncPipeline pipeline;
bool ok = execute_pipeline(src, ctx, observer_lambda, &pipeline);
if (pipeline.has_nodes()) {
    ok = run_async_pipeline(ctx, pipeline);
}
```

Add `has_nodes()` to AsyncPipeline:
```cpp
bool has_nodes() const { return !nodes_.empty() || !deferred_.empty(); }
```

### Step 1.3: Delete `run_observable()`, `execute_observable_async()`, `needs_async()`

These are no longer called. Remove them.

### Validation

All 1361 tests pass. Behavior identical. The new function is a thin wrapper.

---

## Phase 2: Migrate Transforms (Batch — 21 operators)

**Goal:** Move all non-temporal transforms into `execute_pipeline()` with `shared_ptr` state. Remove from both old functions.

### Operators (21)

| Operator | State | Notes |
|---|---|---|
| Map | `xt*` (from obs, no heap needed) | Observer wraps execute_xt |
| MapWith | `xt*`, `Value context` | context addref'd per call |
| Filter | `xt*` | Predicate check, value addref/release |
| FilterWith | `xt*`, `Value context` | Same as Filter + context |
| Scan | `shared_ptr<Value> accum` | Accumulator updated per value |
| Take | `shared_ptr<int64_t> remaining` | Decrement per value |
| Skip | `shared_ptr<int64_t> to_skip` | Decrement per value |
| Distinct | `shared_ptr<pair<bool, Value>>` | Track previous value |
| DistinctUntil | `shared_ptr<pair<bool, Value>>` | Same structure as Distinct |
| Tap | `xt*` | Side-effect, forward unchanged |
| StartWith | `Value prepend`, `shared_ptr<bool>` | Emit prepend once |
| Pairwise | `shared_ptr<pair<bool, Value>>` | Track previous for pairing |
| First | None (returns false after 1) | Equivalent to Take(1) |
| Last | `shared_ptr<pair<bool, Value>>` | Buffer last, emit on complete |
| TakeWhile | `xt*` | Predicate check |
| Finalize | `xt*` | Cleanup on completion |
| Catch | `xt*` | Error recovery |
| Buffer | `shared_ptr<HeapArray*>`, `int64_t count` | Batch into arrays |
| BufferWhen | `shared_ptr<HeapArray*>`, `xt*` | Predicate-triggered flush |
| Window | `shared_ptr<...>` | Sliding window |
| Timestamp | None | Wall-clock annotation |
| TimeInterval | `shared_ptr<time_point>` | Delta timing |

### Implementation Pattern

Each operator follows the same pattern:

```cpp
case K::OperatorName: {
    // Allocate state on heap
    auto state = std::make_shared<OperatorState>(...);
    // Build observer wrapper
    Observer wrapped = [state, observer](Value v, ExecutionContext& c) -> bool {
        // operator logic (identical to current sync logic)
    };
    // Recurse — works for both sync and async
    return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
}
```

For operators with completion semantics (Last, Distinct, Finalize, Buffer), the "on complete" logic currently runs after `execute_observable()` returns. In the unified model, this needs to happen when the source completes. Two approaches:

**Approach A**: Same as current — if `pipeline == nullptr`, the recursive call blocks until complete, and the on-complete code runs after. If `pipeline != nullptr`, register a completion callback on the source nodes.

**Approach B (simpler)**: For these operators in async mode, fall through to IdleNode collection (existing default behavior). They work correctly, just not interleaved. Migrate them to full async in a later phase.

**Recommendation**: Approach A for simple cases (Distinct, Last), Approach B for complex cases (Buffer, Window). Note which operators use Approach B for future cleanup.

### Removal from Old Functions

After each operator is migrated to `execute_pipeline()`:
1. Remove the case from `execute_observable()`
2. Remove the case from `build_async_pipeline()` (if present)

When an old function has no cases left, delete it.

### Validation

All 1361 tests pass after each batch of migrations. Run the async merge/zip/temporal tests to verify interleaving still works.

---

## Phase 3: Migrate Sources (5 operators)

**Goal:** Move source execution into `execute_pipeline()` with `if (pipeline)` branches.

### Operators (5)

| Operator | Sync Behavior | Async Behavior |
|---|---|---|
| Timer | Blocking sleep loop | `TimerNode` with `uv_timer_t` |
| Range | Direct emission loop | `IdleNode` with pre-collected values |
| FromArray | Iterate HeapArray | `IdleNode` with pre-collected values |
| Of | Single emission | `IdleNode` with single value |
| Empty | Return true | No node (immediate completion) |

### Implementation

```cpp
case K::Timer: {
    if (pipeline) {
        auto node = std::make_unique<TimerNode>();
        // ... configure delay_ms, period_ms, observer ...
        pipeline->add_node(std::move(node));
        return true;
    }
    // Sync: blocking loop (existing code)
    // ...
}

case K::Range: {
    int64_t start = obs->state().as_int;
    int64_t end = obs->param();
    if (pipeline) {
        auto node = std::make_unique<IdleNode>();
        for (int64_t i = start; i < end; ++i)
            node->values.push_back(Value(i));
        node->observer = observer;
        node->ctx = &ctx;
        pipeline->add_node(std::move(node));
        return true;
    }
    for (int64_t i = start; i < end; ++i) {
        if (!ctx.tick()) return false;
        if (!observer(Value(i), ctx)) return true;
    }
    return true;
}
```

### Validation

All tests pass. Async merge/zip with Range and Timer sources work.

---

## Phase 4: Migrate Combination and Temporal Operators (13 operators)

**Goal:** Move remaining operators with `if (pipeline)` branches.

### Combination (3)

| Operator | Sync | Async |
|---|---|---|
| Merge | Sequential | Both sources registered concurrently |
| Concat | Sequential | Sequential via IdleNode collection |
| Zip | Collect-then-pair | Concurrent buffered pairing (ZipState) |

### Temporal Transforms (10)

Each has the same structure: `if (pipeline)` creates a `TransformTimerNode` and wraps the observer with timer-driven logic. Else uses the existing `sleep_until_or_tick` synchronous path.

| Operator | Timer Pattern |
|---|---|
| Delay | One-shot per value |
| DelayEach | Computed one-shot per value |
| DebounceTime | Reset on each value |
| ThrottleTime | Leading-edge gate |
| SampleTime | Periodic emission |
| AuditTime | Trailing-edge |
| BufferTime | Periodic flush |
| TakeUntilTime | One-shot deadline |
| Timeout | Resettable deadline |
| RetryDelay | Sync fallback (dynamic retry deferred to Phase 5) |

### Validation

All tests pass. Run full temporal async test suite.

---

## Phase 5: Dynamic Nodes — FlatMap, SwitchMap, RetryDelay

**Goal:** Implement true async FlatMap, SwitchMap, and RetryDelay using recursive `execute_pipeline()` calls during the poll loop.

### Step 5.1: AsyncPipeline Extensions

Add node groups and runtime registration:

```cpp
using GroupId = uint32_t;

GroupId start_new_group();          // Begin tracking new nodes
void end_group(GroupId id);         // Stop tracking, associate nodes
void stop_group(GroupId id);        // Stop all handles in group
void remove_group(GroupId id);      // Close + remove group's nodes
void register_new_nodes();          // Init unregistered nodes on loop
```

### Step 5.2: FlatMap

```cpp
case K::FlatMap: {
    auto* xt = obs->operator_xt();
    if (pipeline) {
        Observer wrapped = [xt, observer, pipeline, &ctx](Value v, ExecutionContext& c) -> bool {
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) return false;
            auto inner_val = c.data_stack().pop();
            auto* inner = inner_val->as_observable();
            // Build inner observable into the running pipeline
            execute_pipeline(inner, ctx, observer, pipeline);
            pipeline->register_new_nodes();
            inner->release();
            return true;
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }
    // Sync fallback (existing code)
    // ...
}
```

### Step 5.3: SwitchMap

Same as FlatMap but stops the previous inner group before creating a new one:

```cpp
auto current_group = std::make_shared<GroupId>(0);
Observer wrapped = [xt, observer, pipeline, current_group, &ctx](Value v, ExecutionContext& c) -> bool {
    // Cancel previous inner
    if (*current_group != 0) {
        pipeline->stop_group(*current_group);
        pipeline->remove_group(*current_group);
    }
    // Build new inner
    *current_group = pipeline->start_new_group();
    c.data_stack().push(v);
    if (!execute_xt(xt, c)) return false;
    auto inner_val = c.data_stack().pop();
    auto* inner = inner_val->as_observable();
    execute_pipeline(inner, ctx, observer, pipeline);
    pipeline->end_group(*current_group);
    pipeline->register_new_nodes();
    inner->release();
    return true;
};
```

### Step 5.4: RetryDelay (Async)

On source failure, create a one-shot `uv_timer_t` for the delay. Timer callback re-calls `execute_pipeline()` on the source, registering new nodes.

### Validation

- FlatMap with timed inner observables: verify concurrent inner execution
- SwitchMap with timed source + timed inner: verify previous inner is cancelled
- RetryDelay with timed source: verify retry after delay
- All 1361 existing tests pass

---

## Phase 6: Cleanup

**Goal:** Delete the old functions. Single execution path.

### Step 6.1: Delete Dead Code

Remove:
- `execute_observable()` — replaced by `execute_pipeline(..., nullptr)`
- `build_async_pipeline()` — replaced by `execute_pipeline(..., &pipeline)`
- `needs_async()` — no longer needed
- `run_observable()` — already deleted in Phase 1
- `execute_observable_async()` — already deleted in Phase 1
- Any unreachable helper functions

### Step 6.2: Rename

Optionally rename `execute_pipeline()` to `execute_observable()` for backward compatibility with the mental model.

### Step 6.3: File I/O and HTTP Operators

ReadBytes, ReadLines, ReadJson, ReadCsv, ReadDir, HttpGet, HttpPost, HttpSse — these are already sync-only in the unified function. They can be left as-is (sync sources that emit via direct calls). If needed in async pipelines, they would create IdleNodes to collect their output — handled by the default path.

### Validation

- File compiles with no references to deleted functions
- All 1361 tests pass
- Verify async tests still work (merge, zip, temporal)

---

## Version and Branching

| Phase | Version | Branch |
|---|---|---|
| Phase 1: Scaffold | v1.11.0 | `YYYYMMDDA_obs_unified_exec` |
| Phase 2: Transforms | v1.11.0 | Same branch |
| Phase 3: Sources | v1.11.0 | Same branch |
| Phase 4: Combination + Temporal | v1.11.0 | Same branch |
| Phase 5: Dynamic nodes | v1.11.0 | Same branch |
| Phase 6: Cleanup | v1.11.0 | Same branch |
| Merge to master | v1.11.0 | `master` |

Single feature branch. Each phase is one or more commits. Merge to master after Phase 6 with all tests green.

---

## Line Count Projections

| Component | Current | After Unification |
|---|---|---|
| `execute_observable()` | 1066 lines, 54 cases | Deleted |
| `build_async_pipeline()` | 647 lines, 27 cases | Deleted |
| `execute_pipeline()` | — | ~1100 lines, 54 cases |
| `needs_async()` | 20 lines | Deleted |
| `run_observable()` | 6 lines | Deleted |
| `execute_observable_async()` | 5 lines | Deleted |
| AsyncPipeline class | 100 lines | ~150 lines (+ groups) |
| **Net change** | ~1850 lines | ~1250 lines (**-600 lines**) |

---

## Execution Dependencies

```
Phase 1: Scaffold ──────────────── zero behavior change
    │
    ▼
Phase 2: Transforms (21 ops) ──── mechanical migration, batch
    │
    ▼
Phase 3: Sources (5 ops) ──────── if (pipeline) branches
    │
    ▼
Phase 4: Combo + Temporal (13) ── if (pipeline) branches + timer nodes
    │
    ▼
Phase 5: Dynamic nodes (3 ops) ── new capability: FlatMap, SwitchMap, RetryDelay
    │
    ▼
Phase 6: Cleanup ──────────────── delete old functions
```

Each phase depends on the previous. Phase 2 is the bulk of the work (21 operators, but mechanical). Phase 5 is the payoff (3 operators gain async capability).

**Minimum viable delivery:** Phase 1-4 eliminates duplication and simplifies the code. Phase 5-6 adds new capability and completes the cleanup.

---

## Estimated Effort

| Phase | Solo Human | AI-Assisted |
|---|---|---|
| Phase 1: Scaffold + route terminals | 2 hours | 30 min |
| Phase 2: Migrate 21 transforms | 6 hours | 2 hours |
| Phase 3: Migrate 5 sources | 2 hours | 30 min |
| Phase 4: Migrate 13 combo + temporal | 4 hours | 1.5 hours |
| Phase 5: Dynamic nodes (FlatMap, SwitchMap, RetryDelay) | 8 hours | 3 hours |
| Phase 6: Cleanup + validation | 2 hours | 30 min |
| **Total** | **~24 hours** | **~8 hours** |
