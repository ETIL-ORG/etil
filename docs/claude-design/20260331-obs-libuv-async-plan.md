# Observable libuv Async — Implementation Plan

**Date:** 2026-03-31
**Design Doc:** `20260331-obs-libuv-async-design.md`
**Status:** Planned

---

## Overview

Migrate observable temporal execution from blocking `sleep_until_or_tick()` to libuv event loop callbacks. Fix `obs-merge` to provide true concurrent source subscription. Four phases, each independently testable and committable.

---

## Phase 1: Infrastructure (No Behavior Change)

**Goal:** Build the async execution framework. All existing tests pass unchanged.

### Step 1.1: `needs_async()` Pipeline Scanner

**File:** `src/core/observable_primitives.cpp`

Add a static function that recursively walks the observable tree and returns true if any node requires async execution:

```cpp
static bool needs_async(const HeapObservable* obs);
```

Nodes that trigger async: `Merge`, `Timer`, `Delay`, `DelayEach`, `DebounceTime`, `ThrottleTime`, `SampleTime`, `AuditTime`, `BufferTime`, `TakeUntilTime`, `RetryDelay`, `Timeout`.

Recursion follows `source()` and `source_b()` pointers.

### Step 1.2: `AsyncPipeline` Class

**File:** `src/core/observable_primitives.cpp` (local to file, not in header)

Holds libuv handles and per-node state for async execution:

- `TimerNode` — wraps a `uv_timer_t` for timed sources/operators
- `IdleNode` — wraps a `uv_idle_t` for synchronous sources that need to emit into the event loop
- Handle lifecycle management: init, start, stop, close
- Completion tracking: `sources_active` counter, `stopped` flag, `error` flag
- `build()` — recursive tree walk, creates nodes and wires observer chains
- `register_handles(uv_loop_t*)` — activates all handles on the loop
- `stop_all()` — stops all active handles (early termination)
- `cleanup(uv_loop_t*)` — close all handles, drain pending callbacks

### Step 1.3: `run_async_pipeline()` Poll Loop

**File:** `src/core/observable_primitives.cpp`

```cpp
static bool run_async_pipeline(ExecutionContext& ctx, AsyncPipeline& pipeline);
```

Core loop:
1. `pipeline.register_handles(loop)`
2. While `!pipeline.complete()`:
   - `uv_run(loop, UV_RUN_NOWAIT)` — service callbacks
   - `ctx.tick()` — check execution limits
   - `pipeline.stopped()` — check subscriber stop signal
   - `sleep_for(50us)` — yield
3. `pipeline.cleanup(loop)`

### Step 1.4: `execute_observable_async()` Entry Point

**File:** `src/core/observable_primitives.cpp`

```cpp
static bool execute_observable_async(HeapObservable* obs, ExecutionContext& ctx, const Observer& observer);
```

Builds an `AsyncPipeline` from the observable tree and calls `run_async_pipeline()`.

### Step 1.5: Dispatch in `obs-subscribe`

**File:** `src/core/observable_primitives.cpp` (modify `prim_obs_subscribe`)

Before calling `execute_observable()`, check `needs_async(src)`. If true, call `execute_observable_async()` instead.

Also apply to `prim_obs_to_array`, `prim_obs_count`, `prim_obs_reduce`, and any other terminal operators that call `execute_observable()`.

### Validation

- Run full test suite — all 1361 tests pass
- `needs_async()` returns false for all existing test pipelines (no async nodes)
- Synchronous path is taken for all existing code
- Zero behavior change

---

## Phase 2: Timer + Merge

**Goal:** Timer emits via `uv_timer_t`. Merge subscribes to both sources concurrently. `obs-merge` bug is fixed.

### Step 2.1: `TimerNode` Implementation

Wire `uv_timer_t` for `Kind::Timer` sources:

```
uv_timer_init(loop, &node->handle)
uv_timer_start(&node->handle, timer_cb, delay_ms, period_ms)
```

Timer callback:
- Emit `Value(counter++)` through the observer chain
- If observer returns false (subscriber stopped), `uv_timer_stop()`
- If period == 0 (one-shot), `uv_timer_stop()` after first emission

Handle `obs-interval` (= Timer with delay=0) naturally.

### Step 2.2: `IdleNode` for Synchronous Sources

Synchronous sources (FromArray, Range, Of) in an async pipeline emit via `uv_idle_t`:

- `build()` collects all values from the synchronous source into a vector
- `uv_idle_t` callback emits one value per loop iteration
- When all values emitted, `uv_idle_stop()` and signal source completion

This allows synchronous sources to interleave with timed sources in the same event loop.

### Step 2.3: Async Merge

`build()` for `Kind::Merge`:

1. Build sub-pipeline for source A — creates TimerNode or IdleNode
2. Build sub-pipeline for source B — creates TimerNode or IdleNode
3. Both use the same downstream observer (the merge observer)
4. Track `sources_active = 2`
5. When a source completes: `sources_active--`
6. When `sources_active == 0`: merge is complete

`max_concurrent` handling:
- If `max_concurrent >= 2`: register both sources' handles immediately
- If `max_concurrent == 1`: register source A first. On source A completion callback, register source B.

The merge observer forwards values from either source to the downstream observer. If the downstream observer returns false (e.g., `obs-take` satisfied), set `stopped = true` and stop all source handles.

### Step 2.4: Observer Chain Wrapping in Async Mode

Transform operators (Map, Filter, Take, etc.) wrap the observer callback in async mode identically to sync mode. The `build()` method constructs the same lambda chain — only source registration differs.

Example: `obs-interval | obs-map(dup) | obs-take(5) | obs-subscribe(.)`

- `build()` walks the tree bottom-up:
  1. Subscribe creates terminal observer (push + execute Xt)
  2. Take wraps it (counting observer, returns false after 5)
  3. Map wraps that (push value, execute dup Xt, pop result, forward)
  4. Timer registers `uv_timer_t` with Map's observer as callback target

### Validation

**Test A: Sync + Sync merge**
```til
0 3 obs-range  0 3 obs-range  1000 obs-merge
['] . obs-subscribe
```
All 6 values emitted. Order: interleaved (both idle handles fire on alternating iterations).

**Test B: Timed + Sync merge**
```til
1000000 obs-interval  0 5 obs-range  1000 obs-merge
10 obs-take  ['] . obs-subscribe
```
Range values arrive first (idle), then interval values arrive at 1s intervals. Total 10 values.

**Test C: Two infinite sources + take**
```til
1000000 obs-interval  500000 obs-interval  1000 obs-merge
5 obs-take  ['] . obs-subscribe
```
5 values total. Both timers cleaned up. No hang.

**Test D: max_concurrent = 1**
```til
0 3 obs-range  0 3 obs-range  1 obs-merge
['] . obs-subscribe
```
Output: 0 1 2 0 1 2 (source A completes, then source B starts).

**Test E: Existing test script**
```til
include til-scripts/test-merge-notify.til
```
Interleaved interval and range values with notification prefixes.

**Test F: Full regression**
All 1361 existing tests pass.

---

## Phase 3: Temporal Transforms

**Goal:** Migrate all rate-limiting and delay operators from `sleep_until_or_tick()` to `uv_timer_t`.

### Step 3.1: Delay / DelayEach

**Delay**: For each upstream value, start a one-shot `uv_timer_t` with `delay_ms`. Timer callback forwards the delayed value to the downstream observer.

Challenge: Multiple values may be in-flight simultaneously (upstream emits faster than the delay). Each value gets its own timer handle. Values are delivered in order (FIFO) because libuv timers with the same due time fire in registration order.

**DelayEach**: Same as Delay but the delay duration is computed per-value by executing the Xt. The Xt must run synchronously during the upstream callback (before the timer is registered).

### Step 3.2: DebounceTime

Maintains a single `uv_timer_t` that resets on each upstream value.

- On upstream value: `uv_timer_stop()`, store value, `uv_timer_start(handle, cb, quiet_ms, 0)`
- Timer callback: emit stored value to downstream observer
- On upstream complete: emit stored value immediately (if any), stop timer

### Step 3.3: ThrottleTime

Leading-edge throttle with a gate timer.

- On upstream value:
  - If gate is open (no active timer): emit value, start gate timer `uv_timer_start(handle, cb, window_ms, 0)`
  - If gate is closed (timer active): suppress value
- Gate timer callback: mark gate as open (no emission — leading edge already emitted)

### Step 3.4: SampleTime

Periodic timer that emits the most recent upstream value.

- `uv_timer_start(handle, cb, period_ms, period_ms)` — repeating timer
- On upstream value: store as latest value (overwrite previous)
- Timer callback: if a new value has arrived since last sample, emit it
- On upstream complete: stop timer, emit final value if unsent

### Step 3.5: AuditTime

Trailing-edge throttle — like ThrottleTime but emits the *last* value in the window.

- On upstream value:
  - Store value
  - If no timer active: start timer `uv_timer_start(handle, cb, window_ms, 0)`
- Timer callback: emit stored value, mark timer inactive

### Step 3.6: BufferTime

Periodic timer that flushes an accumulated buffer.

- `uv_timer_start(handle, cb, period_ms, period_ms)` — repeating timer
- On upstream value: append to buffer (HeapArray)
- Timer callback: emit buffer as HeapArray, create new empty buffer
- On upstream complete: emit remaining buffer, stop timer

### Step 3.7: TakeUntilTime

One-shot deadline timer.

- `uv_timer_start(handle, cb, duration_ms, 0)` — one-shot
- On upstream value: forward to downstream
- Timer callback: set `stopped = true`, stop all upstream handles

### Step 3.8: Timeout

One-shot timer that resets on each upstream value.

- `uv_timer_start(handle, cb, limit_ms, 0)` — one-shot
- On upstream value: `uv_timer_stop()`, forward value, `uv_timer_start()` (reset)
- Timer callback: timeout exceeded — signal error, stop pipeline

### Step 3.9: RetryDelay

One-shot timer between retry attempts.

- On upstream failure: start `uv_timer_start(handle, cb, delay_ms, 0)`
- Timer callback: re-register upstream source on the event loop (retry)
- Track retry count. After max retries, signal failure.

### Validation

- Run existing temporal observable tests — behavior identical (single-source pipelines)
- Verify `sleep_until_or_tick()` is no longer called by any operator
- Test merged timed+timed sources: e.g., `1000000 obs-interval` merged with `500000 obs-interval` — values should interleave at correct intervals
- Test temporal transforms on merged sources: e.g., `merge | obs-debounce-time`

### Cleanup

Remove `sleep_until_or_tick()` function after all callers are migrated.

---

## Phase 4: Combination Operators

**Goal:** Migrate Concat, Zip, FlatMap, and SwitchMap to event-loop-driven execution.

### Step 4.1: Async Concat

Source B is registered on the event loop only after source A's completion callback fires. Semantically unchanged but driven by event loop rather than blocking recursion.

### Step 4.2: Async Zip

Both sources registered concurrently. Each source's values are buffered in per-source queues. When both queues are non-empty, pair the front values and emit.

On one source completing: emit remaining pairs if the other source still has values, then signal zip completion.

### Step 4.3: Async FlatMap

For each upstream value, execute Xt to produce an inner observable. Register the inner observable on the event loop. Multiple inner observables can be active simultaneously. All inner emissions forwarded to downstream.

`max_concurrent` (if exposed as a parameter) limits simultaneous inner subscriptions. Default: unlimited.

### Step 4.4: Async SwitchMap

Like FlatMap but only the latest inner observable is active. On new upstream value: stop the current inner observable's handles, build and register the new inner observable.

### Validation

- Test Zip with two timed sources at different rates
- Test FlatMap where inner observables are timed
- Test SwitchMap cancellation of previous inner
- Full regression

---

## Version and Branching

| Phase | Version | Branch |
|---|---|---|
| Phase 1 | v1.10.0 | `20260331A_obs_libuv_async` |
| Phase 2 | v1.10.1 | Same branch |
| Phase 3 | v1.10.2 | Same branch |
| Phase 4 | v1.10.3 | Same branch |
| Merge to master | v1.10.3 | `master` |

Single feature branch. Each phase is a commit (or small set of commits) on the branch. Merge to master after Phase 4 with all tests green.

---

## Execution Order and Dependencies

```
Phase 1: Infrastructure ──────────────── no behavior change, all tests pass
    │
    ▼
Phase 2: Timer + Merge ───────────────── obs-merge bug fix, new tests
    │
    ▼
Phase 3: Temporal Transforms ─────────── migrates 10 operators, removes sleep_until_or_tick
    │
    ▼
Phase 4: Combination Operators ────────── async concat/zip/flatmap/switchmap
```

Each phase depends on the previous. Phase 2 is the critical bug fix. Phase 3 is the bulk of the work (10 operators). Phase 4 is incremental improvement.

**Minimum viable delivery:** Phase 1 + Phase 2 fixes the `obs-merge` bug and provides the event loop infrastructure for future work. Phases 3 and 4 can follow in a separate session.

---

## Test Plan Summary

| Phase | New Tests | Regression |
|---|---|---|
| 1 | `needs_async()` unit test — returns false for all existing pipelines | All 1361 pass |
| 2 | 5 merge tests (sync+sync, timed+sync, infinite+take, max_concurrent=1, test script) | All 1361 pass |
| 3 | Temporal tests with merged timed sources | All 1361 pass + temporal behavior verified |
| 4 | Zip/FlatMap/SwitchMap with timed sources | All pass |

---

## Estimated Effort

| Phase | Solo Human | AI-Assisted |
|---|---|---|
| Phase 1: Infrastructure | 6 hours | 2 hours |
| Phase 2: Timer + Merge | 8 hours | 3 hours |
| Phase 3: Temporal transforms (10 operators) | 10 hours | 4 hours |
| Phase 4: Combination operators | 6 hours | 2 hours |
| Testing across all phases | 8 hours | 3 hours |
| **Total** | **~38 hours** | **~14 hours** |
