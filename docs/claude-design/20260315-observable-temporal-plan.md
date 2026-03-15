# Plan: Implement Observable Temporal Operators

## Context

The ETIL Observable system (v0.9.0, 21 words) is entirely synchronous. This plan adds temporal operators (timer, interval, delay, timestamp, and rate-limiting words) to enable real wall-clock time in observable pipelines. The full design is in `docs/claude-design/20260315-observable-temporal-design.md`.

## Implementation: 3 Stages

### Stage 1: Core Temporal (4 words + 1 helper)

**Files to modify:**

| File | Changes |
|------|---------|
| `include/etil/core/heap_observable.hpp` | Add Kind enum values: `Timer`, `Delay`, `Timestamp`, `TimeInterval`. Add factory methods: `timer()`, `delay()`, `timestamp()`, `time_interval()`. |
| `src/core/observable_primitives.cpp` | Add `sleep_until_or_tick()` helper. Add execution cases for Timer, Delay, Timestamp, TimeInterval. Add 4 `prim_obs_*` functions. Register 4 new words. Update `kind_name()`. |
| `data/builtins.til` | Add `: obs-interval 0 swap obs-timer ;` |
| `data/help.til` | Add help entries for `obs-timer`, `obs-interval`, `obs-delay`, `obs-timestamp` |
| `tests/unit/test_observable_primitives.cpp` | ~8 new test cases |
| `tests/til/test_observable.til` | ~6 new integration tests |

**New TIL words:**
- `obs-timer ( delay-us period-us -- obs )` — C++ Kind::Timer
- `obs-interval ( period-us -- obs )` — Self-hosted sugar
- `obs-delay ( obs delay-us -- obs' )` — C++ Kind::Delay
- `obs-timestamp ( obs -- obs' )` — C++ Kind::Timestamp

**Key implementation: `sleep_until_or_tick()`** — cooperative wait helper (~15 lines) that sleeps in 1ms increments while calling `ctx.tick()` for budget/deadline/cancel enforcement.

### Stage 2: Rate-Limiting (4 words)

**Same files as Stage 1, plus:**

| File | Changes |
|------|---------|
| `include/etil/core/heap_observable.hpp` | Add Kinds: `DebounceTime`, `ThrottleTime`, `SampleTime`, `Timeout` + factories |
| `src/core/observable_primitives.cpp` | 4 execution cases + 4 primitives + registration |
| `data/help.til` | 4 new help entries |
| `tests/unit/test_observable_primitives.cpp` | ~8 new tests |
| `tests/til/test_observable.til` | ~4 new integration tests |

**New TIL words:**
- `obs-debounce-time ( obs quiet-us -- obs' )`
- `obs-throttle-time ( obs window-us -- obs' )`
- `obs-sample-time ( obs period-us -- obs' )`
- `obs-timeout ( obs limit-us -- obs' )`

### Stage 3: Windowed + Additional (6 words)

**Same files, adding:**

| File | Changes |
|------|---------|
| `include/etil/core/heap_observable.hpp` | Add Kinds: `BufferTime`, `TakeUntilTime`, `DelayEach`, `AuditTime`, `RetryDelay` + factories |
| `src/core/observable_primitives.cpp` | 6 execution cases + 6 primitives + registration |
| `data/builtins.til` | Optional ms-convenience words |
| `data/help.til` | 6 new help entries |
| `tests/unit/test_observable_primitives.cpp` | ~12 new tests |
| `tests/til/test_observable.til` | ~6 new integration tests |

**New TIL words:**
- `obs-buffer-time ( obs window-us -- obs' )`
- `obs-time-interval ( obs -- obs' )`
- `obs-take-until-time ( obs duration-us -- obs' )`
- `obs-delay-each ( obs xt -- obs' )`
- `obs-audit-time ( obs window-us -- obs' )`
- `obs-retry-delay ( obs delay-us max-retries -- obs' )`

## Key Patterns to Reuse

- **`execute_observable()` recursive engine** — `observable_primitives.cpp:173` — all new cases added here
- **`HeapObservable` factory pattern** — each Kind gets a static factory, stores params in `state_`/`param_`/`operator_xt_`
- **`make_primitive()` registration** — `primitives.hpp` — used by `register_observable_primitives()`
- **`HeapArray` for wrapping** — `obs-timestamp` and `obs-time-interval` emit `[time, value]` pairs as 2-element arrays (same pattern as `obs-zip`)
- **`pop_observable()` helper** — `heap_observable.hpp:92` — stack pop with type check
- **`std::chrono::system_clock::now()` for wall-clock** — same as `time-us` primitive
- **`std::chrono::steady_clock::now()` for intervals** — monotonic, not affected by NTP adjustments

## Verification

```bash
# Build
ninja -C build-debug

# Run all tests
ctest --test-dir build-debug --output-on-failure

# Run just observable tests
./build-debug/bin/etil_tests --gtest_filter=Observable*

# Run TIL integration tests
ctest --test-dir build-debug --output-on-failure -R observable

# Manual REPL verification
./build-debug/bin/etil_repl
# > 0 1000000 obs-timer 3 obs-take obs-to-array .s
# > 500000 obs-interval 5 obs-take obs-timestamp obs-to-array .s
```
