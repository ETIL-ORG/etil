# Observable Async Execution via libuv Event Loop

**Date:** 2026-03-31
**Status:** Design
**Bug:** obs-merge acts identically to obs-concat (sequential, not concurrent)
**Scope:** Full migration of observable temporal execution to libuv event loop

---

## Problem

`obs-merge` currently runs source A to completion, then source B — identical to `obs-concat`:

```cpp
case K::Merge: {
    // Phase 1 (synchronous): sequential merge — same as concat.
    bool ok = execute_observable(obs->source(), ctx, observer);
    if (!ok) return false;
    return execute_observable(obs->source_b(), ctx, observer);
}
```

This defeats the purpose of merge. True merge subscribes to both sources concurrently and emits values from whichever source produces first. `obs-merge` completes only when both input streams complete. If either source is infinite (e.g., `obs-interval`), merge never completes — by design.

### Root Cause

The observable execution model is entirely synchronous and pull-based. `execute_observable()` is a recursive function that blocks on each source. Temporal operators use `sleep_until_or_tick()` — a busy-wait loop with 1ms `std::this_thread::sleep_for` granularity. There is no event loop, no multiplexing, no concurrent source execution.

This is not just a merge problem. The same limitation affects:
- **obs-zip with timed sources**: Should pair values by arrival time, currently drains A then B
- **obs-switch-map**: Should cancel previous inner observable when new value arrives, currently runs sequentially
- **obs-flat-map**: Should run inner observables concurrently, currently runs them sequentially
- **Future multi-source operators**: Any operator that needs concurrent subscription

### Why This Matters

Async operations — concurrent streams, timed interleaving, reactive event handling — are a major reason for implementing observables in the first place. Without true async execution, observables are just iterators with extra steps.

---

## Current Architecture

### Execution Model

```
obs-subscribe
  └── execute_observable(obs, ctx, observer)
        └── recursive switch on obs->kind()
              ├── Source (FromArray, Timer, Range, ...): generates values, calls observer
              ├── Transform (Map, Filter, ...): wraps observer, calls upstream
              ├── Combination (Merge, Concat, Zip): calls upstream(s) sequentially
              └── Terminal (Take, Skip, ...): limits observer calls, calls upstream
```

Every operator is a synchronous, blocking recursive call. The observer callback chain is built backwards (terminal → source) and executed forwards (source → terminal). Values flow through the chain one at a time.

### Temporal Operators

5 operators use `sleep_until_or_tick()` for wall-clock delays:

| Operator | How It Sleeps |
|---|---|
| Timer | Initial delay, then period between emissions |
| Delay | Fixed delay before forwarding each value |
| DelayEach | Per-value delay computed by Xt |
| TakeUntilTime | Implicit via upstream timeout |
| RetryDelay | Delay between retry attempts |

4 rate-limiting operators track wall-clock time but don't sleep — they make synchronous decisions on each upstream value:

| Operator | Behavior |
|---|---|
| DebounceTime | Suppresses values during quiet period |
| ThrottleTime | Leading-edge gate |
| SampleTime | Emits latest value on period boundary |
| AuditTime | Trailing-edge gate |

### `sleep_until_or_tick()`

```cpp
static bool sleep_until_or_tick(ExecutionContext& ctx,
                                 std::chrono::steady_clock::time_point target) {
    while (std::chrono::steady_clock::now() < target) {
        if (!ctx.tick()) return false;
        auto remaining = target - std::chrono::steady_clock::now();
        if (remaining > std::chrono::milliseconds(1)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return true;
}
```

This blocks the entire interpreter thread. While Timer is sleeping 1 second between emissions, nothing else can happen — no other observable can emit, no I/O can complete, no other work can proceed.

### libuv Infrastructure (Already Present)

ETIL already links libuv and has:

| Component | Location | Purpose |
|---|---|---|
| `UvSession` | `include/etil/fileio/uv_session.hpp` | Per-session `uv_loop_t` wrapper |
| `FsRequest` | Same file | Async file I/O with atomic completion flag |
| `WorkRequest` | Same file | Thread pool work with completion tracking |
| `await_completion()` | `src/fileio/uv_session.cpp` | `UV_RUN_NOWAIT` + `ctx.tick()` poll loop |
| `await_work()` | Same file | Same pattern for thread pool work |
| `ctx.uv_session()` | `execution_context.hpp` | Accessor to per-session UvSession |

The `await_completion()` pattern is the key — it already does exactly what we need: poll the libuv event loop with `UV_RUN_NOWAIT`, check `ctx.tick()`, yield briefly, repeat. This is the cooperative async pattern.

---

## Design: Event-Loop-Driven Observable Execution

### Core Idea

Replace `sleep_until_or_tick()` with `uv_timer_t` handles on the session's event loop. Replace the blocking recursive execution model with an event-driven model where:

1. **Sources** register libuv handles (timers, idle callbacks) that produce values
2. **The event loop** drives execution — `uv_run(UV_RUN_NOWAIT)` in a cooperative poll loop
3. **Values flow through the observer chain** on the event loop thread (interpreter thread) — no multithreading
4. **Multiple sources** can have active handles simultaneously — this is how merge works

### No Multithreading

Everything runs on the interpreter thread. The libuv event loop is polled cooperatively via `UV_RUN_NOWAIT`. Timer callbacks fire on the interpreter thread when polled. No mutexes, no shared state, no thread safety issues.

### Two Execution Modes

Not all operators need the event loop. Synchronous operators (FromArray, Range, Map, Filter, Take, etc.) work fine as-is. Only operators that need concurrent source execution or wall-clock timing need the event loop.

**Synchronous mode** (unchanged): `execute_observable()` as it exists today. Used for pure data pipelines with no temporal or concurrent operators.

**Async mode**: A new `execute_observable_async()` that drives execution from the event loop. Used when the pipeline contains Merge, Timer, or other async-requiring operators.

The pipeline is scanned at subscription time to determine which mode to use. If any node in the pipeline requires async, the entire pipeline runs in async mode.

### Async Execution Architecture

```
obs-subscribe
  └── scan pipeline for async nodes
  └── if async required:
        ├── build async pipeline (register libuv handles)
        └── run_async_loop(ctx):
              while (!pipeline_complete && ctx.tick()):
                  uv_run(loop, UV_RUN_NOWAIT)
                  sleep_for(50us)  // avoid busy-spin
```

Each async-capable operator registers state on the event loop:

```
┌─────────────────────────────────────────────────┐
│                  uv_loop_t                      │
│                                                 │
│  ┌─────────────┐  ┌─────────────┐              │
│  │ uv_timer_t  │  │ uv_timer_t  │              │
│  │ (source A:  │  │ (source B:  │              │
│  │  interval   │  │  interval   │              │
│  │  1s period) │  │  500ms per) │              │
│  └──────┬──────┘  └──────┬──────┘              │
│         │                │                      │
│         ▼                ▼                      │
│  ┌──────────────────────────────────┐          │
│  │      Merge collector             │          │
│  │  (observer callback — forwards   │          │
│  │   values from either source      │          │
│  │   to downstream observer)        │          │
│  └──────────────┬───────────────────┘          │
│                 │                               │
│                 ▼                               │
│  ┌──────────────────────────────────┐          │
│  │      Take(10)                    │          │
│  │  (counts emissions, returns     │          │
│  │   false after 10 → stops loop)  │          │
│  └──────────────┬───────────────────┘          │
│                 │                               │
│                 ▼                               │
│  ┌──────────────────────────────────┐          │
│  │      Subscriber observer         │          │
│  │  (pushes value, executes Xt)    │          │
│  └──────────────────────────────────┘          │
│                                                 │
└─────────────────────────────────────────────────┘
```

### Timer on the Event Loop

Current Timer uses a blocking loop:

```cpp
case K::Timer: {
    if (delay_us > 0) sleep_until_or_tick(ctx, target);
    observer(Value(0), ctx);
    while (true) {
        sleep_until_or_tick(ctx, next_tick);   // blocks entire thread
        observer(Value(counter++), ctx);
        next_tick += period_us;
    }
}
```

Async Timer registers a `uv_timer_t`:

```cpp
struct TimerState {
    uv_timer_t handle;
    int64_t counter = 0;
    Observer observer;
    ExecutionContext* ctx;
    bool stopped = false;
};

static void timer_callback(uv_timer_t* handle) {
    auto* state = static_cast<TimerState*>(handle->data);
    if (state->stopped) return;
    if (!state->observer(Value(state->counter++), *state->ctx)) {
        state->stopped = true;
        uv_timer_stop(handle);
    }
}

// Registration:
uv_timer_init(loop, &state->handle);
uv_timer_start(&state->handle, timer_callback, delay_ms, period_ms);
```

Now the timer fires via the event loop. Between firings, the loop is free to service other handles — including another timer from a merged source.

### How Merge Works

Merge registers both sources on the event loop. Both sources' handles are active simultaneously. The event loop fires whichever timer expires first.

```cpp
// Merge setup:
// 1. Build async pipeline for source A → registers timer_a on loop
// 2. Build async pipeline for source B → registers timer_b on loop
// 3. Both timers fire into the same downstream observer chain
// 4. Poll loop runs until both sources complete or subscriber stops

struct MergeState {
    std::atomic<int> sources_active{2};
    Observer downstream;
    bool stopped = false;
};

// Source A's observer:
auto merge_observer_a = [&merge](Value v, ExecutionContext& ctx) -> bool {
    if (merge.stopped) { value_release(v); return false; }
    if (!merge.downstream(v, ctx)) { merge.stopped = true; return false; }
    return true;
};

// Source B's observer: identical, same MergeState
// When a source completes: sources_active--
// When sources_active == 0: merge is complete
```

With `max_concurrent`: if set to 1, source B is not registered until source A's completion callback fires. The event loop naturally serializes them without blocking.

### Synchronous Sources in Async Mode

A synchronous source (e.g., `obs-range`) in an async pipeline needs to emit all its values during the setup phase, before the poll loop starts. This is done via a `uv_idle_t` handle:

```cpp
struct IdleEmitter {
    uv_idle_t handle;
    std::vector<Value> values;
    size_t index = 0;
    Observer observer;
    ExecutionContext* ctx;
};

static void idle_callback(uv_idle_t* handle) {
    auto* state = static_cast<IdleEmitter*>(handle->data);
    if (state->index < state->values.size()) {
        state->observer(state->values[state->index++], *state->ctx);
    } else {
        uv_idle_stop(handle);  // source complete
    }
}
```

The idle handle fires once per loop iteration, emitting one value per poll. This interleaves naturally with timer handles — the event loop services all active handles each iteration.

### Synchronous Transforms in Async Mode

Transform operators (Map, Filter, Scan, etc.) don't need event loop handles. They wrap the observer callback exactly as they do today. The async pipeline builds the same observer chain; only sources register handles.

```
Timer(1s) ──→ Map(dup) ──→ Take(5) ──→ Subscribe(.)

Async setup:
  1. Subscribe creates final observer
  2. Take wraps it (counting observer)
  3. Map wraps that (transform observer)
  4. Timer registers uv_timer_t with Map's observer as callback
  5. Poll loop runs event loop
  6. Timer fires → Map observer → Take observer → Subscribe observer
```

The observer chain is identical to the synchronous case. Only the source registration changes.

---

## Pipeline Scan: Sync vs Async

At subscription time, walk the observable tree to detect async-requiring nodes:

```cpp
bool needs_async(const HeapObservable* obs) {
    if (!obs) return false;
    switch (obs->kind()) {
        case Kind::Merge:           // concurrent sources
        case Kind::Timer:           // timed emissions
        case Kind::Delay:           // timed delay
        case Kind::DelayEach:       // per-item delay
        case Kind::DebounceTime:    // needs timed flush
        case Kind::ThrottleTime:    // needs timed gate
        case Kind::SampleTime:      // needs timed sampling
        case Kind::AuditTime:       // needs timed trailing edge
        case Kind::BufferTime:      // needs timed flush
        case Kind::TakeUntilTime:   // needs timed deadline
        case Kind::RetryDelay:      // needs timed retry
        case Kind::Timeout:         // needs timed deadline
            return true;
        default:
            break;
    }
    return needs_async(obs->source()) || needs_async(obs->source_b());
}
```

If `needs_async()` returns false, the pipeline runs via the existing synchronous `execute_observable()` — zero overhead for pure data pipelines.

---

## Async-Enabled Operator Changes

### Sources

| Operator | Current | Async |
|---|---|---|
| Timer | Blocking `sleep_until_or_tick` loop | `uv_timer_t` with period callback |
| FromArray | Synchronous loop | `uv_idle_t`, one value per loop iteration |
| Range | Synchronous loop | `uv_idle_t`, one value per loop iteration |
| Of | Single emission | `uv_idle_t`, single fire |
| Empty | No emission | Immediate completion |

### Combination Operators

| Operator | Current | Async |
|---|---|---|
| Merge | Sequential (= concat) | Both sources registered concurrently on loop. `max_concurrent` controls simultaneous registrations. Completes when all sources complete. |
| Concat | Sequential | Source B registered after source A completes. Unchanged semantics, but event-loop-driven. |
| Zip | Collect-both-then-pair | Both sources registered concurrently. Values buffered per-source. Pair emitted when both buffers non-empty. |

### Temporal Transforms

| Operator | Current | Async |
|---|---|---|
| Delay | `sleep_until_or_tick` per value | `uv_timer_t` per value (one-shot, delay_ms) |
| DebounceTime | Synchronous time check | `uv_timer_t` reset on each value. Fires after quiet period to emit last value. |
| ThrottleTime | Synchronous gate check | `uv_timer_t` for gate expiry. Leading-edge emit, suppress until timer fires. |
| SampleTime | Synchronous period check | `uv_timer_t` periodic. Emits latest buffered value on each fire. |
| AuditTime | Synchronous trailing check | `uv_timer_t` one-shot on first value. Trailing-edge emit when timer fires. |
| BufferTime | Synchronous window check | `uv_timer_t` periodic. Flushes accumulated buffer on each fire. |
| TakeUntilTime | Upstream-driven | `uv_timer_t` one-shot deadline. Stops pipeline when timer fires. |
| DelayEach | `sleep_until_or_tick` per value | `uv_timer_t` per value (one-shot, computed delay) |
| RetryDelay | `sleep_until_or_tick` between retries | `uv_timer_t` one-shot between retry attempts |
| Timeout | ExecutionContext deadline | `uv_timer_t` one-shot. Stops pipeline if no emission before timer fires. |

### Operators That Don't Change

All synchronous transform, limiting, accumulate, file I/O, and HTTP operators continue wrapping the observer callback exactly as today. They don't register libuv handles — they just transform values as they flow through.

| Category | Operators | Change |
|---|---|---|
| Transform | Map, MapWith, Filter, FilterWith | None — observer wrapper |
| Accumulate | Scan | None — observer wrapper |
| Limiting | Take, Skip, Distinct, TakeWhile, DistinctUntil, First, Last | None — observer wrapper |
| Side-effect | Tap, StartWith, Finalize, Catch | None — observer wrapper |
| Composition | FlatMap, SwitchMap, Buffer, BufferWhen, Window, Pairwise | None — observer wrapper |
| File I/O | ReadBytes, ReadLines, ReadJson, ReadCsv, ReadDir | None — already use libuv via UvSession |
| HTTP | HttpGet, HttpPost, HttpSse | None — use cpp-httplib |

---

## The Async Poll Loop

The central execution loop replaces `obs-subscribe`'s call to `execute_observable()` when async is needed:

```cpp
bool run_async_pipeline(ExecutionContext& ctx, AsyncPipeline& pipeline) {
    auto* loop = ctx.uv_session()->loop();

    // Register all source handles on the event loop
    pipeline.register_handles(loop);

    // Poll until pipeline completes, subscriber stops, or tick fails
    while (!pipeline.complete()) {
        // Service libuv handles — timer callbacks fire here
        uv_run(loop, UV_RUN_NOWAIT);

        // Check execution limits
        if (!ctx.tick()) {
            pipeline.stop_all();
            break;
        }

        // Check if subscriber signaled stop (e.g., obs-take)
        if (pipeline.stopped()) break;

        // Yield to avoid busy-spinning (50us is negligible vs timer granularity)
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    // Cleanup: stop all handles, close them
    pipeline.cleanup(loop);

    return !pipeline.had_error();
}
```

This is the same pattern as `UvSession::await_completion()` — already proven in the codebase.

---

## AsyncPipeline: State Machine

An `AsyncPipeline` holds the libuv handles and state for all async nodes in the pipeline:

```cpp
class AsyncPipeline {
public:
    // Build from observable tree
    void build(HeapObservable* obs, ExecutionContext& ctx, const Observer& terminal);

    // Register all handles on the event loop
    void register_handles(uv_loop_t* loop);

    // Stop all active sources (early termination)
    void stop_all();

    // Cleanup: stop + close all handles
    void cleanup(uv_loop_t* loop);

    bool complete() const;   // all sources finished
    bool stopped() const;    // subscriber signaled stop
    bool had_error() const;

private:
    struct TimerNode {
        uv_timer_t handle{};
        int64_t counter = 0;
        int64_t delay_us = 0;
        int64_t period_us = 0;
        Observer observer;
        bool done = false;
    };

    struct IdleNode {
        uv_idle_t handle{};
        std::vector<Value> values;
        size_t index = 0;
        Observer observer;
        bool done = false;
    };

    std::vector<std::unique_ptr<TimerNode>> timers_;
    std::vector<std::unique_ptr<IdleNode>> idles_;
    bool stopped_ = false;
    bool error_ = false;
};
```

`build()` walks the observable tree recursively. At each node:
- **Source nodes** create a `TimerNode` or `IdleNode`
- **Transform/limiting nodes** wrap the observer callback (same as synchronous mode)
- **Merge/Concat/Zip** build sub-pipelines for each source

---

## Merge Semantics (Detailed)

```
obs-a  obs-b  max-concurrent  obs-merge
```

### max_concurrent >= 2 (true merge)

Both sources are registered on the event loop simultaneously. Whichever source's handle fires first, its value flows through the downstream observer chain. Order is determined by the event loop's timer resolution.

**Example**: Timer(1s) merged with Range(0,5):

```
Time 0ms:   Range emits 0 (idle handle, fires immediately)
Time 0ms:   Range emits 1
Time 0ms:   Range emits 2
Time 0ms:   Range emits 3
Time 0ms:   Range emits 4
Time 0ms:   Range complete (idle stopped)
Time 1000ms: Timer emits 0 (first period)
Time 2000ms: Timer emits 1
...
```

Range values arrive first because idle handles fire before timers on the same loop iteration. Timer values continue indefinitely until the subscriber stops.

### max_concurrent == 1 (sequential)

Source A is registered first. When source A completes, source B is registered. Semantically identical to `obs-concat`, but driven by the event loop instead of blocking recursive calls.

### Completion

Merge tracks `sources_active` count. Each source decrements on completion. When `sources_active == 0`, the merge node signals completion. If the subscriber returns false (e.g., `obs-take`), all source handles are stopped immediately.

---

## Migration Path

### Phase 1: Infrastructure (no behavior change)

1. Add `AsyncPipeline` class
2. Add `needs_async()` pipeline scanner
3. Add `run_async_pipeline()` poll loop
4. Add `execute_observable_async()` entry point
5. Modify `obs-subscribe` to check `needs_async()` and dispatch accordingly

**All existing tests pass unchanged** — pure data pipelines use the synchronous path.

### Phase 2: Timer + Merge

1. Implement `TimerNode` with `uv_timer_t`
2. Implement `IdleNode` for sync sources in async pipelines
3. Implement async Merge with concurrent source registration
4. Fix `obs-merge` to use async execution

**Key tests**: merge of timed + sync sources, merge with obs-take, merge of two infinite sources with obs-take.

### Phase 3: Temporal Transforms

Migrate rate-limiting operators to use `uv_timer_t`:

1. DebounceTime — timer reset on each value
2. ThrottleTime — timer for gate expiry
3. SampleTime — periodic timer
4. AuditTime — trailing-edge timer
5. BufferTime — periodic flush timer
6. Delay / DelayEach — per-value one-shot timer
7. TakeUntilTime — deadline timer
8. Timeout — deadline timer
9. RetryDelay — retry timer

Each migrated operator stops using `sleep_until_or_tick()` and registers a `uv_timer_t` instead.

### Phase 4: Combination Operators

1. Async Zip — concurrent collection with pairing
2. Async Concat — event-loop-driven sequential (source B registered after source A completes)
3. Async FlatMap / SwitchMap — inner observable registration on event loop

---

## Impact on Existing Code

### Files Modified

| File | Change |
|---|---|
| `src/core/observable_primitives.cpp` | Add `AsyncPipeline`, `needs_async()`, `run_async_pipeline()`, `execute_observable_async()`. Modify `K::Merge`, `K::Timer`, temporal operator cases. |
| `include/etil/core/heap_observable.hpp` | No change — Kind enum, node structure unchanged |
| `include/etil/fileio/uv_session.hpp` | Possibly add helper for timer handle management |
| `src/core/observable_primitives.cpp` (subscribe) | Dispatch to async path when `needs_async()` |

### Files Not Modified

| File | Why |
|---|---|
| All non-observable source files | Observable changes are self-contained |
| `execution_context.hpp` | `uv_session()` accessor already exists |
| `heap_observable.hpp` | Node structure and Kind enum unchanged |
| TIL word interfaces | No stack effect changes |

### Backward Compatibility

- **Pure data pipelines**: Zero change. `needs_async()` returns false, synchronous path used.
- **Existing temporal tests**: Behavior should be identical (single-source pipelines have no concurrency difference).
- **obs-merge callers**: Behavior changes from sequential to concurrent — this is the bug fix.
- **Performance**: Async path adds ~50us polling overhead per loop iteration. Negligible for timed streams. Pure data pipelines unaffected.

### References

- [libuv Documentation](https://docs.libuv.org/en/v1.x/)

---

## Risks

1. **libuv timer resolution**: `uv_timer_t` has millisecond resolution. Microsecond-precision delays (current `sleep_until_or_tick` achieves ~1ms) may lose precision. For observable use cases (UI events, polling, sampling), millisecond resolution is sufficient.

2. **Handle cleanup on early termination**: `obs-take` stopping a merge must close all active `uv_timer_t` handles before returning. libuv requires handles to be closed before loop destruction. The `AsyncPipeline::cleanup()` method handles this.

3. **Nested async pipelines**: FlatMap/SwitchMap can create inner observables that themselves need async. The inner pipeline must register on the same loop. This is naturally supported — `uv_timer_init` can be called at any time on a running loop.

4. **Stack depth during callbacks**: Timer callbacks fire during `uv_run()`, which calls the observer chain synchronously. Deep observer chains (many Map/Filter layers) add stack frames during the callback. Same depth as the synchronous model — no new risk.

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
