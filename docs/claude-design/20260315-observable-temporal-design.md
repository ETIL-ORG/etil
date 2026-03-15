# ETIL Observable Temporal Operators — Design Document

## Date: 2026-03-15

## Context

Extend the ETIL Observable system (v0.9.0, 21 words) with **temporal operators** inspired by RxJS. The current observable pipeline is entirely synchronous — all emissions happen immediately when a terminal operator is called. Temporal operators introduce real wall-clock time into the pipeline: delays, intervals, rate-limiting, and time-stamping.

### Existing Infrastructure

| Component | Status | Relevance |
|-----------|--------|-----------|
| `HeapObservable` (16 Kinds) | Implemented | New Kinds added here |
| `execute_observable()` push-based engine | Implemented | Temporal cases added here |
| `ctx.tick()` execution limits | Implemented | Budget/deadline enforcement during waits |
| `sleep` primitive | Implemented | Cooperative wait with deadline check |
| libuv `UvSession` + `await_work()` | Implemented | Cooperative timer polling pattern |
| `time-us` primitive | Implemented | Microsecond wall-clock source |
| `ctx.deadline()` | Implemented | Hard execution time limit |

### Design Constraints

1. **Single-threaded per ExecutionContext** — no concurrent emission from multiple sources.
2. **`ctx.tick()` must be called during waits** — instruction budget, deadline, and cancellation must be respected.
3. **MCP sessions have 30-second deadlines** — long-running temporal pipelines must be interruptible.
4. **No libuv requirement for basic temporal ops** — `sleep` uses `std::this_thread::sleep_for`, which suffices for cooperative waiting. Reserve libuv timers for a future async phase.
5. **Lazy pipeline construction** — temporal operators build nodes; time only passes during terminal execution.

---

## Proposed Word Inventory

### Tier 1 — Core Temporal (4 words)

The user's primary targets. High suitability, essential for any temporal observable system.

| Word | Stack Effect | RxJS Analog | Description |
|------|-------------|-------------|-------------|
| `obs-timer` | `( delay-us period-us -- obs )` | `timer(delay, period?)` | After initial delay, emit 0; if period > 0, repeat (1, 2, 3...) on cadence. Period 0 = single shot. |
| `obs-interval` | `( period-us -- obs )` | `interval(period)` | Emit 0, 1, 2... on fixed cadence. Sugar for `0 swap obs-timer`. |
| `obs-delay` | `( obs delay-us -- obs' )` | `delay(due)` | Shift each emission forward by delay microseconds. |
| `obs-timestamp` | `( obs -- obs' )` | `timestamp()` | Wrap each value as `[time-us, value]` 2-element array. |

### Tier 2 — Rate-Limiting (4 words)

High utility for event-driven patterns. Medium implementation complexity — require tracking wall-clock time between emissions.

| Word | Stack Effect | RxJS Analog | Description |
|------|-------------|-------------|-------------|
| `obs-debounce-time` | `( obs quiet-us -- obs' )` | `debounceTime(ms)` | Emit only after no emission for quiet-us microseconds. |
| `obs-throttle-time` | `( obs window-us -- obs' )` | `throttleTime(ms)` | Emit first value, suppress for window-us, then allow next. Leading-edge. |
| `obs-sample-time` | `( obs period-us -- obs' )` | `sampleTime(ms)` | On each period tick, emit the most recent upstream value (if any new). |
| `obs-timeout` | `( obs limit-us -- obs' )` | `timeout({each})` | If no upstream emission within limit-us, cancel pipeline (return false). |

### Tier 3 — Time-Windowed Collection (2 words)

Useful for batching and instrumentation. Moderate complexity.

| Word | Stack Effect | RxJS Analog | Description |
|------|-------------|-------------|-------------|
| `obs-buffer-time` | `( obs window-us -- obs' )` | `bufferTime(ms)` | Collect emissions into arrays, emit array at each window boundary. |
| `obs-time-interval` | `( obs -- obs' )` | `timeInterval()` | Wrap each value as `[elapsed-us, value]` where elapsed-us is delta since last emission. |

### Tier 4 — Suggested Additional Words (4 words)

Beyond the RxJS temporal set, these leverage ETIL's existing infrastructure and would be immediately useful.

| Word | Stack Effect | Category | Description |
|------|-------------|----------|-------------|
| `obs-take-until-time` | `( obs duration-us -- obs' )` | Limiting | Forward emissions for duration-us, then complete. Simpler than `take-until(timer())`. |
| `obs-delay-each` | `( obs xt -- obs' )` | Transform | Per-item delay: xt receives value, returns delay-us. Analog of `delayWhen`. |
| `obs-audit-time` | `( obs window-us -- obs' )` | Rate-Limiting | After first emission, wait window-us, emit the *last* value seen. Like throttle but trailing-edge. |
| `obs-retry-delay` | `( obs delay-us max-retries -- obs' )` | Error | On pipeline failure, wait delay-us and retry up to max-retries times. Backoff pattern. |

---

## Categorization Summary

### By Function

| Category | Words |
|----------|-------|
| **Source (creation)** | `obs-timer`, `obs-interval` |
| **Transform (per-emission)** | `obs-delay`, `obs-timestamp`, `obs-time-interval`, `obs-delay-each` |
| **Rate-Limiting (emission control)** | `obs-debounce-time`, `obs-throttle-time`, `obs-sample-time`, `obs-audit-time` |
| **Windowed Collection** | `obs-buffer-time` |
| **Limiting (pipeline lifecycle)** | `obs-timeout`, `obs-take-until-time` |
| **Error Recovery** | `obs-retry-delay` |

### By Difficulty

| Difficulty | Words | Notes |
|------------|-------|-------|
| **Easy** | `obs-timestamp`, `obs-time-interval`, `obs-interval` | Pure value wrapping or simple `obs-timer` sugar. No cooperative wait logic. |
| **Medium** | `obs-timer`, `obs-delay`, `obs-take-until-time`, `obs-timeout` | Cooperative wait using `sleep_until_or_tick()`. Straightforward timer integration. |
| **Hard** | `obs-debounce-time`, `obs-throttle-time`, `obs-sample-time`, `obs-audit-time`, `obs-buffer-time`, `obs-delay-each` | Require tracking wall-clock timestamps across multiple upstream emissions. Must handle edge cases (empty windows, rapid bursts). |
| **Hard+** | `obs-retry-delay` | Requires re-executing the upstream pipeline from scratch. Needs careful refcount management on retry. |

### By Suitability for ETIL's Synchronous Model

| Suitability | Words | Rationale |
|-------------|-------|-----------|
| **Excellent** | `obs-timestamp`, `obs-time-interval`, `obs-interval`, `obs-timer` | Natural fit — emit-then-wait is inherently sequential. |
| **Good** | `obs-delay`, `obs-timeout`, `obs-take-until-time`, `obs-buffer-time` | Work well synchronously; just add waits between emissions. |
| **Acceptable** | `obs-debounce-time`, `obs-throttle-time`, `obs-sample-time`, `obs-audit-time` | Meaningful only when upstream is itself temporal (e.g., piped after `obs-interval`). With instantaneous sources like `obs-from`, these degenerate. |
| **Limited** | `obs-delay-each`, `obs-retry-delay` | `delay-each` needs a way to compute delay per item. `retry-delay` needs pipeline re-execution. Both work but are more complex. |

---

## Implementation Approach

### Cooperative Wait Helper

All temporal operators share a common need: wait for a duration while respecting `ctx.tick()` and `ctx.deadline()`. Extract this into a reusable helper:

```cpp
/// Sleep until target time or until tick() fails (budget/deadline/cancel).
/// Returns true if the target time was reached, false if interrupted.
bool sleep_until_or_tick(ExecutionContext& ctx,
                         std::chrono::steady_clock::time_point target) {
    while (std::chrono::steady_clock::now() < target) {
        if (!ctx.tick()) return false;
        // Sleep in small increments to keep tick() responsive
        auto remaining = target - std::chrono::steady_clock::now();
        if (remaining > std::chrono::milliseconds(1)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return true;
}
```

**Why 1ms granularity?** The `tick()` amortization mask (`& 0x3FFF`) means `tick()` only checks the clock every ~16K calls. In a busy loop calling `tick()` directly, we'd spin the CPU. Sleeping 1ms between checks gives ~1000 tick checks per second — responsive enough for microsecond-precision delays while keeping CPU usage near zero.

**Alternative: libuv timers.** For a future async phase, replace `sleep_for` with `uv_timer_start` + `uv_run(UV_RUN_NOWAIT)` polling. This would integrate with the event loop and allow other async operations (file I/O, HTTP) to proceed during waits. The `sleep_until_or_tick` helper can be swapped without changing the observable execution logic.

### HeapObservable Kind Extensions

```cpp
enum class Kind {
    // Existing (16)
    FromArray, Of, Empty, Range,
    Map, MapWith, Filter, FilterWith,
    Scan, Reduce,
    Take, Skip, Distinct,
    Merge, Concat, Zip,

    // Tier 1: Core Temporal
    Timer,          // delay in state_.as_int (us), period in param_ (us, 0=one-shot)
    Delay,          // delay in param_ (us)
    Timestamp,      // no extra state
    TimeInterval,   // no extra state (delta computed at execution time)

    // Tier 2: Rate-Limiting
    DebounceTime,   // quiet window in param_ (us)
    ThrottleTime,   // throttle window in param_ (us)
    SampleTime,     // sample period in param_ (us)
    Timeout,        // timeout limit in param_ (us)

    // Tier 3: Windowed
    BufferTime,     // window size in param_ (us)

    // Tier 4: Additional
    TakeUntilTime,  // duration in param_ (us)
    DelayEach,      // xt in operator_xt_
    AuditTime,      // window in param_ (us)
    RetryDelay,     // delay in state_.as_int (us), max retries in param_
};
```

### Execution Engine Cases

#### Timer (creation source)

```cpp
case Kind::Timer: {
    int64_t delay_us = obs->state().as_int;
    int64_t period_us = obs->param();
    auto start = std::chrono::steady_clock::now();

    // Initial delay
    if (delay_us > 0) {
        auto target = start + std::chrono::microseconds(delay_us);
        if (!sleep_until_or_tick(ctx, target)) return false;
    }

    // Emit 0
    if (!observer(Value(int64_t(0)), ctx)) return false;

    if (period_us <= 0) return true;  // one-shot

    // Repeating emissions
    int64_t counter = 1;
    auto next_tick = std::chrono::steady_clock::now()
                   + std::chrono::microseconds(period_us);
    while (true) {
        if (!sleep_until_or_tick(ctx, next_tick)) return false;
        if (!observer(Value(counter++), ctx)) return false;
        next_tick += std::chrono::microseconds(period_us);
    }
    // Infinite source — only exits via take, timeout, or cancellation
}
```

#### Delay (transform)

```cpp
case Kind::Delay: {
    int64_t delay_us = obs->param();
    return execute_observable(obs->source(), ctx,
        [&](Value v, ExecutionContext& ctx) -> bool {
            auto target = std::chrono::steady_clock::now()
                        + std::chrono::microseconds(delay_us);
            if (!sleep_until_or_tick(ctx, target)) {
                value_release(v);
                return false;
            }
            return observer(v, ctx);
        });
}
```

#### Timestamp (transform — no wait)

```cpp
case Kind::Timestamp: {
    return execute_observable(obs->source(), ctx,
        [&](Value v, ExecutionContext& ctx) -> bool {
            auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto* pair = new HeapArray();
            pair->push_back(Value(int64_t(now_us)));
            pair->push_back(v);
            return observer(Value::from(pair), ctx);
        });
}
```

#### DebounceTime (rate-limiting)

```cpp
case Kind::DebounceTime: {
    int64_t quiet_us = obs->param();
    Value last_value = {};
    bool has_value = false;

    bool completed = execute_observable(obs->source(), ctx,
        [&](Value v, ExecutionContext& ctx) -> bool {
            if (has_value) value_release(last_value);
            last_value = v;
            value_addref(last_value);
            has_value = true;
            // Don't emit — wait for silence
            return true;
        });

    // Source completed; if we have a pending value, emit it
    // (debounce: emit after silence, source completion = infinite silence)
    if (has_value) {
        bool ok = observer(last_value, ctx);
        // last_value ref transferred to observer
        if (!ok) return false;
    }

    return completed;
}
```

**Note on debounce semantics in synchronous mode:** With an instantaneous source (e.g., `obs-from`), all emissions arrive back-to-back with zero time between them. Every value is "within the quiet window" of the previous one, so debounce suppresses all but effectively emits the last value on completion. This is correct RxJS behavior — debounce is designed for sources that have real time gaps. When piped after `obs-interval` or `obs-timer`, debounce works as expected.

#### ThrottleTime (rate-limiting)

```cpp
case Kind::ThrottleTime: {
    int64_t window_us = obs->param();
    auto gate_until = std::chrono::steady_clock::time_point::min();  // open gate

    return execute_observable(obs->source(), ctx,
        [&](Value v, ExecutionContext& ctx) -> bool {
            auto now = std::chrono::steady_clock::now();
            if (now >= gate_until) {
                // Gate is open: emit and close gate
                gate_until = now + std::chrono::microseconds(window_us);
                return observer(v, ctx);
            }
            // Gate closed: suppress
            value_release(v);
            return true;
        });
}
```

---

## Design Alternatives Considered

### Alternative A: Virtual Time (Tick-Based)

Define "time" in terms of instruction ticks rather than wall-clock microseconds. `obs-timer 1000` would mean "emit after 1000 ticks" rather than "emit after 1000 microseconds."

**Pros:**
- Fully deterministic — no timing jitter
- No blocking — executes at full speed
- Trivially testable (set instruction budget, count emissions)
- No `sleep` dependency

**Cons:**
- Not real time — useless for actual delays (sensor polling, heartbeats)
- Confusing semantics — "1000" means different things on different hardware
- Breaks the RxJS mental model where time is wall-clock

**Verdict:** Rejected as the primary model. Could be offered as a separate `obs-tick-timer` in the future for testing/simulation, but the primary temporal operators should use real wall-clock time to match user expectations.

### Alternative B: libuv Timer Integration

Use `uv_timer_start()` + `uv_run(UV_RUN_NOWAIT)` polling instead of `sleep_for`.

**Pros:**
- Integrates with the existing libuv event loop
- Allows other async operations (file I/O, HTTP) during waits
- Natural path to true async observables (Phase 2)
- More precise timer semantics (OS-level timer queue)

**Cons:**
- Requires `UvSession` (not available in standalone REPL without libuv init)
- More complex — timer callback state management
- Minimal benefit in single-threaded synchronous mode

**Verdict:** Deferred to Phase 2 (async observables). The `sleep_until_or_tick` approach works correctly and the helper can be replaced with a libuv-based implementation later without changing the observable execution logic.

### Alternative C: Callback-Based Async (Full Rewrite)

Rewrite the execution engine to be fully asynchronous with callbacks/coroutines.

**Pros:**
- True concurrent emission (multiple sources interleaving)
- `obs-merge` becomes meaningful (not just sequential)
- Natural hot observable support
- Full RxJS feature parity

**Cons:**
- Massive architectural change — touches every operator
- C++20 coroutines are complex and have poor library support
- Breaks the simple recursive push model
- ETIL's single-threaded-per-context model would need fundamental rethinking

**Verdict:** Out of scope. The synchronous cooperative model serves ETIL's current needs. True async observables would be a Phase 3 effort requiring significant design work.

### Alternative D: Separate Temporal Source + Compose

Instead of temporal *operators*, provide only `obs-timer` and `obs-interval` as temporal *sources*, then use existing operators to compose temporal behavior:

```forth
\ "debounce" via composition
obs-interval obs-zip ...  \ doesn't really work
```

**Pros:**
- Minimal new code — only 2 new Kind values
- Leverages existing operator set

**Cons:**
- Most temporal behaviors (debounce, throttle, sample) cannot be expressed as simple compositions of existing operators — they require internal state tracking of wall-clock time
- Forces users to build complex workarounds
- Doesn't match the RxJS API users expect

**Verdict:** Rejected. Temporal operators need dedicated implementations because they maintain wall-clock state that cannot be factored through existing operators.

---

## Implementation Stages

### Stage 1: Core Temporal (v0.9.5)

**4 new HeapObservable Kinds + 4 TIL words + 1 helper:**

| Item | Effort |
|------|--------|
| `sleep_until_or_tick()` helper | ~15 lines |
| `obs-timer` (Kind::Timer) | ~30 lines execution + ~15 lines factory/primitive |
| `obs-interval` (Kind::Interval sugar) | Self-hosted: `: obs-interval 0 swap obs-timer ;` |
| `obs-delay` (Kind::Delay) | ~15 lines execution + ~10 lines factory/primitive |
| `obs-timestamp` (Kind::Timestamp) | ~10 lines execution + ~10 lines factory/primitive |
| Unit tests | ~150 lines (timer one-shot, timer repeating, delay, timestamp, cancellation) |
| TIL integration tests | ~50 lines |
| help.til entries | ~40 lines |

**Estimated total:** ~350 lines of new code.

**`obs-interval` as self-hosted word:**
```forth
: obs-interval ( period-us -- obs )
    0 swap obs-timer ;
```

This avoids a redundant Kind. `obs-timer 0 period` = emit 0 immediately, then repeat.

### Stage 2: Rate-Limiting (v0.9.6)

**4 new Kinds + 4 TIL words:**

| Item | Effort |
|------|--------|
| `obs-debounce-time` | ~25 lines execution |
| `obs-throttle-time` | ~20 lines execution |
| `obs-sample-time` | ~30 lines execution |
| `obs-timeout` | ~15 lines execution |
| Factory methods + primitives | ~40 lines |
| Unit tests | ~200 lines |
| TIL integration tests | ~60 lines |
| help.til entries | ~50 lines |

**Estimated total:** ~440 lines.

### Stage 3: Time-Windowed + Additional (v0.9.7)

**6 new Kinds + 6 TIL words:**

| Item | Effort |
|------|--------|
| `obs-buffer-time` | ~30 lines execution |
| `obs-time-interval` | ~15 lines execution |
| `obs-take-until-time` | ~15 lines execution |
| `obs-delay-each` | ~20 lines execution |
| `obs-audit-time` | ~25 lines execution |
| `obs-retry-delay` | ~35 lines execution |
| Factory methods + primitives | ~60 lines |
| Unit tests | ~250 lines |
| TIL integration tests | ~80 lines |
| help.til entries | ~70 lines |

**Estimated total:** ~600 lines.

---

## Testing Strategy

### Unit Tests (C++)

Each temporal operator needs:
1. **Basic behavior** — e.g., `obs-timer` with 0 delay emits immediately
2. **Cancellation** — pipeline respects `ctx.tick()` during waits
3. **Deadline enforcement** — timer exceeding `ctx.deadline()` is interrupted
4. **Composition** — temporal + existing operators (e.g., `obs-timer ... obs-take 5 obs-to-array`)
5. **Edge cases** — zero delay, zero period, empty upstream

### TIL Integration Tests

```forth
\ Timer: single-shot emits 0
0 0 obs-timer obs-to-array
array-length 1 expect-eq "obs-timer-single-shot-count"
0 array-get 0 expect-eq "obs-timer-single-shot-value"

\ Interval: first 3 values are 0, 1, 2
1000 obs-interval 3 obs-take obs-to-array
0 array-get 0 expect-eq "obs-interval-first"
1 array-get 1 expect-eq "obs-interval-second"
2 array-get 2 expect-eq "obs-interval-third"

\ Timestamp: wraps value with time
array-new 42 array-push obs-from obs-timestamp obs-to-array
0 array-get  \ [time-us, 42]
1 array-get 42 expect-eq "obs-timestamp-value"
```

### Timing Tests

Temporal tests are inherently sensitive to system load. Strategy:
- Use short delays (1000-10000 us) to keep tests fast
- Assert timing within a tolerance band (e.g., +/- 50%)
- Focus on *ordering* and *count* rather than exact timing
- Use `obs-timer 0 0` (zero delay) for non-timing behavioral tests

---

## TIL Usage Examples

### Heartbeat: emit a timestamp every 500ms, for 5 beats

```forth
500000 obs-interval            \ 0, 1, 2, 3, 4, ...
  5 obs-take                   \ first 5
  obs-timestamp                \ [time-us, counter]
  ' . obs-subscribe            \ print each pair
```

### Delayed greeting

```forth
s" Hello!" obs-of
  2000000 obs-delay            \ 2-second delay
  ' type obs-subscribe cr      \ print after delay
```

### Rate-limited sensor polling (simulated)

```forth
100000 obs-interval            \ 100ms cadence (10 Hz source)
  50 obs-take                  \ 5 seconds of data
  500000 obs-throttle-time     \ throttle to 2 Hz
  obs-count .                  \ ~10 emissions
```

### Timeout: fail if no emission within 1 second

```forth
5000000 0 obs-timer            \ emit after 5 seconds (will timeout)
  1000000 obs-timeout          \ 1-second timeout
  obs-to-array                 \ will fail (return false from execute)
```

### Buffer: collect 1 second of data into arrays

```forth
100000 obs-interval            \ 10 Hz
  30 obs-take                  \ 3 seconds
  1000000 obs-buffer-time      \ 1-second windows → arrays of ~10 items
  ' . obs-subscribe            \ print each array
```

---

## Open Questions

1. **Time units: microseconds or milliseconds?** The existing `time-us` and `sleep` use microseconds. RxJS uses milliseconds. Proposal: **microseconds** for consistency with ETIL convention, but add self-hosted convenience words `obs-timer-ms`, `obs-interval-ms`, `obs-delay-ms` that multiply by 1000.

2. **Infinite source termination:** `obs-timer` with a period and `obs-interval` are infinite sources. They only terminate via `obs-take`, `obs-timeout`, or `ctx.tick()` cancellation. Should we add a built-in emission count limit to the timer factory? Proposal: **No** — keep it simple. Users compose with `obs-take`.

3. **`obs-interval` as self-hosted vs C++ Kind:** Self-hosted (`: obs-interval 0 swap obs-timer ;`) avoids a redundant Kind. But it means `obs-kind` returns `"timer"` instead of `"interval"`. Proposal: **Self-hosted** — simplicity wins. `obs-kind` returning "timer" is acceptable.

4. **Rate-limiting operators with instantaneous sources:** `obs-debounce-time` on `obs-from` is technically correct but degenerate (only emits last value). Should we warn? Proposal: **No** — same as RxJS behavior. Users learn quickly.

5. **Scheduler abstraction?** RxJS has `SchedulerLike` to swap real time for virtual time. Should ETIL have a similar concept? Proposal: **Deferred.** A `TestScheduler` would be valuable but adds complexity. For now, test with short real delays. Revisit when virtual time testing is needed.
