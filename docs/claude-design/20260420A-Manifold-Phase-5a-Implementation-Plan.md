# Manifold Phase 5a — Implementation Plan

**Date:** 2026-04-20
**Status:** Draft, not yet started
**Branch:** `manifold-phase-5a` (already created at v2.8.0)
**Depends on:** Phases 0-4 shipped in v2.4.3 → v2.7.7
**Supersedes:** the first impl attempt that was rolled back on 2026-04-20 because it surfaced concurrency bugs masked by stderr diagnostics; this plan mandates test-first so those classes of bug cannot hide.

Companion design docs (read first):
- `20260418B-IO-Channel-Pipeline-Architecture.md` §24 — architecture gaps this sprint fixes (§24.1 async subscriber dispatch, §24.2 producer-keyed registry, §24.1.1 libuv analysis, §24.1.2 etil-web mapping, §24.1.3 codec boundary question)

---

## 1. Motivation

Two gaps surfaced during interactive field use of v2.7.3:

1. **§24.1 — synchronous subscriber dispatch.** `DefaultChannelService::publish()` invokes subscriber callbacks on the caller's stack. A slow subscriber, a reentrant publish, or an exception in a sink's `accept()` propagates back into the publisher's path. For the MCP server specifically this surfaces as a transport-response hang when a TIL subscriber is attached to `etil.mcp.**`: the interpret call's `etil.mcp.request.completed` publish runs the subscriber before the HTTP response is written, and any `cr`/`type`/`dump` inside the subscriber blocks the chunked writer.

2. **§24.2 — `channel-list` only reflects routed channels.** The channel registry is derived from `add_route`. A process emits hundreds of channel names via MCP/evolution/REPL instrumentation that never enter the registry because no route is installed. Interactive observability — "show me every channel that's receiving publishes, with counts" — isn't possible.

Both gaps have the same blast radius (the `DefaultChannelService::publish` hot path) and the same testing surface (dispatcher + introspection), so they bundle naturally.

## 2. Sprint goals

**In scope:**
- Replace synchronous subscriber/sink dispatch with an async worker-thread model that isolates subscriber execution from the publisher's stack.
- Add producer-keyed channel registry + three new introspection TIL words.
- Extend `channel-cycle-stats` with overflow and queue-depth counters.
- Wire subscriber queue overflow to the existing `RouteSpec` delivery modes (`RingBuffered` / `DropOldest` / `Block`).
- Refactor both new and existing Manifold code to expose a **rich testing surface**: injectable clock, controllable sinks, dispatcher-pause knob, queue-depth observers, exception-injection hooks.
- Mandatory test-first discipline: every concurrency property gets a dedicated failing test BEFORE the production code that makes it pass.
- ThreadSanitizer clean across all new tests (CI gate).

**Out of scope:**
- etil-web Web-Worker implementation. Design for it (keep the interface clean) but don't implement.
- Option (a) from §24.1.3 (universal codec boundary). Defer to Phase 5b.
- Pointer-pass vs serialize subscriber boundary on etil-web — decide during etil-web Phase 5b.
- Sink-side deduplication, subscriber back-pressure signaling to publishers, flow control across broker sinks.

## 3. Architecture delta

### 3.1 Component topology before / after

```
Before (v2.7.7 and earlier):

  publish(msg) ──[sync]──> dispatch() ──[sync]──> for each matched route:
                                                    apply transforms
                                                    sink->accept(msg)   ← on caller's thread

After Phase 5a:

  publish(msg) ──[sync]──> prepare() ──> enqueue(DeliveryItem{...})
                               │                       │
                               └ stamp origin          └ wake dispatcher
                               └ RBAC check            
                               └ layer 1/2 cycle         ╔═════════════════════╗
                               └ match routes            ║  Dispatcher thread  ║
                               └ update producer reg.    ║                     ║
                                                         ║  pop → deliver():   ║
                                                         ║    layer-3 check    ║
                                                         ║    apply transforms ║
                                                         ║    sink->accept()   ║
                                                         ╚═════════════════════╝
```

### 3.2 Interface changes

Public `ChannelService` base class (`include/etil/manifold/service.hpp`) gains:

```cpp
virtual void flush_for_tests() {}   // default no-op for sync impls
```

`CycleStats` (returned by `channel-cycle-stats`) gains four counters:

```cpp
struct CycleStats {
    uint64_t cycles_detected;
    uint64_t ttl_exhausted;
    uint64_t echo_dropped;
    uint64_t static_warnings;
    // new in Phase 5a:
    uint64_t subscriber_queue_depth;       // current dispatcher backlog
    uint64_t dropped_by_overflow;          // RingBuffered/DropOldest drops
    uint64_t dispatcher_exceptions;        // sinks that threw
    uint64_t dispatcher_idle_transitions;  // queue emptied N times
};
```

New TIL words (Phase 5a.5):
- `channel-producer-list ( -- array )` — every channel that has been published to since service start.
- `channel-producer-stats ( name-str -- map )` → `{published_count, last_published_ns, subscriber_count, route_count}`.
- `channel-producers-by-pattern ( pattern-str -- array )` — filter the producer list.

### 3.3 New implementation-level abstractions (for testability)

| Abstraction | File | Purpose |
|---|---|---|
| `IDispatcher` | `include/etil/manifold/dispatcher.hpp` | Pluggable "enqueue + deliver" contract. Prod impl is `ThreadDispatcher`; tests use `InlineDispatcher` (run synchronously for determinism) or `PausableDispatcher` (controlled by test). |
| `IClock` | `include/etil/manifold/clock.hpp` | Abstract source of `steady_clock::now()`. Prod is `SystemClock`; tests use `ManualClock` for deterministic `last_published_ns`. |
| `ExceptionInjectingSink` | `include/etil/manifold/test_sinks.hpp` | Test sink that throws on Nth `accept()` call. |
| `DelayingSink` | same | Test sink that sleeps for N ms in `accept()` to simulate a slow subscriber. |
| `BlockingSink` | same | Test sink whose `accept()` blocks on a test-controlled semaphore until released. |
| `SubscriberCountingSink` | same | Test sink that atomically counts `accept()` calls. |
| `DispatcherIntrospection` | `include/etil/manifold/dispatcher_test_api.hpp` | Friend-only view exposing queue depth, in-flight flag, idle transitions, dispatcher thread ID. Used by the `flush_for_tests()` sanity tests. |

These live in the public `include/` tree so tests don't need to dig into source-only headers.

### 3.4 What does NOT change

- `ChannelService::publish` signature unchanged (callers remain source-compatible).
- `add_route` / `remove_route` / `observe` / RBAC surfaces unchanged.
- Hard-wired channel semantics unchanged.
- Origin tuple + Session-Hmac unchanged.
- Broker sink/source surfaces unchanged.
- `channel-tap-*` TIL words unchanged.

---

## 4. Fine-grained phases

Each phase is a single commit on the `manifold-phase-5a` branch with a patch-level version bump. No phase merges to master until the full sprint is green through the super-push step. The branch head is at v2.8.0 (from `branch.sh`); phases 5a.1 through 5a.10 bring it to v2.8.10.

### Phase 5a.1 — Seam extraction in current synchronous code (v2.8.1)

**Goal:** prepare the code for the refactor WITHOUT changing runtime behavior. Nothing becomes async in this commit.

**Changes:**
1. Extract `DefaultChannelService::deliver()` into a free function `deliver_to_route(RouteState&, const Message&, IClock&)` — easier to test in isolation and needs no reference to the service.
2. Introduce `IClock` + `SystemClock` with a single production instance owned by the service. All calls to `steady_clock::now()` in dispatch, deliver, and audit emission route through the service's `clock_` member.
3. Extract `ExceptionInjectingSink`, `DelayingSink`, `BlockingSink`, `SubscriberCountingSink` into `tests/support/test_sinks.cpp` — standalone, reusable across tests.
4. Add `ChannelService::flush_for_tests()` as a base virtual no-op (for forward compatibility; the sync impl's override is also a no-op).
5. No new behavior. All existing tests still pass.

**Acceptance:**
- `scripts/test.sh all` green.
- `ninja -C build-debug check-tsan` (new target, see §5.3) green.
- `bin/etil_manifold_tests --gtest_list_tests` shows the four new test-sink helpers have at least one smoke test.

**Files touched:** `include/etil/manifold/service.hpp`, `src/manifold/default_service.cpp`, `include/etil/manifold/clock.hpp` (new), `include/etil/manifold/test_sinks.hpp` (new), `tests/support/test_sinks.cpp` (new), `tests/unit/test_manifold_test_sinks.cpp` (new), `tests/CMakeLists.txt`.

### Phase 5a.2 — Async-correctness test suite (xfail) (v2.8.2)

**Goal:** write every test that SHOULD pass once async dispatch ships, then disable them pending Phase 5a.3. A CI/build flag `ETIL_MANIFOLD_ASYNC_DISPATCH_ENABLED` gates whether they run; off until 5a.3.

**New test file:** `tests/unit/test_manifold_async_dispatch.cpp`.

**Tests (each is a failing proof of a property the refactor must satisfy):**

1. **PublishReturnsBeforeSlowSubscriberCompletes** — register a `DelayingSink` on pattern X with 500ms delay; measure `publish(X)` wall time; assert < 50 ms.
2. **ReentrantPublishFromSubscriberDoesNotDeadlock** — subscriber publishes onto channel Y as a side effect. Y also has a sink. Test completes in bounded time, both sinks see their respective message.
3. **FlushForTestsBlocksUntilDispatchedItemsDelivered** — queue 1000 messages; call `flush_for_tests()`; assert all 1000 delivered.
4. **FlushForTestsIsIdempotentWhenIdle** — with no pending items, `flush_for_tests()` returns promptly (< 5 ms).
5. **ConcurrentPublishersInterleave** — 8 threads, each publishes 1000 messages; all 8000 delivered exactly once; no duplicates; no drops (with default RingBuffered capacity > 8000).
6. **DispatcherSurvivesSinkException** — `ExceptionInjectingSink` throws on 5th `accept()`; the other 999 messages still deliver; `dispatcher_exceptions` counter reads 1.
7. **ShutdownDrainsPendingDeliveries** — enqueue 100 messages, immediately call `shutdown_dispatcher_for_tests()`; assert all 100 delivered before the function returns.
8. **ShutdownInterruptsLongBlockingSink** — `BlockingSink` blocks on a never-released semaphore; shutdown still completes within N ms (implementation strategy TBD: detach-and-leak vs timeout).
9. **LayerThreeEchoStateVisibleAfterFlush** — publish with `reject_own_origin=true`; flush; `cycle_stats().echo_dropped` reads 1.
10. **OriginTupleIdenticalAcrossPublisherAndDispatcher** — message's origin stamped in publisher thread equals origin observed by subscriber in dispatcher thread.
11. **ManualClockDrivesLastPublishedNs** — inject `ManualClock` with fixed nanos; assert producer stats carry exactly that value.
12. **SubscriberQueueDepthVisibleInCycleStats** — pause the dispatcher (see `PausableDispatcher`); publish N messages; `cycle_stats().subscriber_queue_depth == N`.

Each test uses `SubscriberCountingSink`, `DelayingSink`, `BlockingSink`, `ExceptionInjectingSink`, or a capture sink from the shared helpers landed in 5a.1. None dig into private state.

**Acceptance:**
- All 12 tests compile.
- With `ETIL_MANIFOLD_ASYNC_DISPATCH_ENABLED=OFF` (default), the test file is `DISABLED_` and `test.sh all` passes.
- With the flag on, tests 1-8 fail (deliberately — sync dispatch can't satisfy them), test 10 passes (already true synchronously), tests 9, 11, 12 fail for different reasons (no flush, no clock injection, no pause).

**Why xfail:** merging failing tests forces everyone (including CI) to see what the refactor owes.

### Phase 5a.3 — Thread dispatcher implementation (v2.8.3)

**Goal:** make every Phase 5a.2 test pass. Existing tests may break; phase 5a.4 fixes them.

**Changes:**
1. `include/etil/manifold/dispatcher.hpp` defines `IDispatcher` abstract:
   ```cpp
   class IDispatcher {
   public:
       virtual ~IDispatcher() = default;
       virtual void enqueue(DeliveryItem item) = 0;
       virtual void flush() = 0;                    // block until drained
       virtual void shutdown() = 0;                 // join / stop worker
       virtual DispatcherStats stats() const = 0;   // queue_depth, exceptions, idle_transitions
   };
   ```
2. `ThreadDispatcher` — the production implementation. Owns one `std::thread`, MPSC queue (`std::mutex` + `std::queue` + `std::condition_variable`), atomic `in_flight_` / `stop_` flags, `drained_cv_` for flush synchronization, per-route overflow-policy application before enqueue.
3. `InlineDispatcher` — test-only alternative that runs `deliver_to_route` synchronously on the caller's thread. Used by tests that want deterministic ordering without needing to flush.
4. `PausableDispatcher` — wraps `ThreadDispatcher` with a test-only pause flag that stops the dispatcher thread from dequeuing; used by queue-depth tests.
5. `DefaultChannelService` constructor takes an optional `std::unique_ptr<IDispatcher>` parameter (defaults to `std::make_unique<ThreadDispatcher>()`). Tests can inject `InlineDispatcher` or `PausableDispatcher`.
6. `DefaultChannelService::flush_for_tests()` override → `dispatcher_->flush()`.
7. Turn the Phase 5a.2 feature flag on.

**Concurrency invariants (must be documented + enforced in comments):**
- I1: every successful `publish()` call results in exactly one `enqueue` per matched route.
- I2: `flush()` returns only after every `enqueue` call that preceded it has had its `deliver()` complete.
- I3: `shutdown()` drains the queue before returning (pending items are delivered; blocking sinks get a best-effort timeout — TBD §8.2).
- I4: origin tuple captured in publisher thread is the EXACT value observed by the sink in dispatcher thread (no per-thread origin state read during delivery).
- I5: `dispatcher_exceptions` increments atomically from the dispatcher thread; publisher never sees a sink exception.
- I6: dispatch order within a single channel is FIFO (publish(m1); publish(m2) on same channel → sink sees m1 before m2). Ordering across channels is not guaranteed (by design — allows parallelism at the dispatcher level if we ever shard).

**Acceptance:**
- All 12 Phase 5a.2 tests pass with `ETIL_MANIFOLD_ASYNC_DISPATCH_ENABLED=ON`.
- TSan clean on the new test binary.
- Existing manifold tests may fail — known, acceptable for this commit only.

### Phase 5a.4 — Existing-test reconciliation (v2.8.4)

**Goal:** every pre-5a Manifold test passes under async dispatch. No test is made flaky; no race is introduced.

**Method:** triage each failing test into three buckets:
- **A.** Test was timing-independent and works unchanged (probably none — async changes visible timing).
- **B.** Test inspects sink state after a publish; needs `svc->flush_for_tests()` between publish and assertion. Mechanical fix.
- **C.** Test asserted on a sync-dispatch artifact (e.g., inspection of publish return value that was only meaningful synchronously, or used a slow sink to prove publisher blocks). Rewrite to assert on post-flush observable behavior, or port to use `InlineDispatcher` if the test is genuinely about dispatch semantics (not async).

**Audit checklist** — run ONCE per file, document findings in commit message:
- `test_manifold_service.cpp`
- `test_manifold_observable.cpp`
- `test_manifold_integration.cpp`
- `test_manifold_integration_phase34.cpp`
- `test_manifold_til.cpp`
- `test_manifold_echo.cpp`
- `test_manifold_rbac.cpp` (likely no changes needed — checks deny paths, not delivery)
- `test_manifold_cycle.cpp` (check layer 1/2 counter timing)
- `test_manifold_sse_in.cpp`
- `test_manifold_nats_sink.cpp` (network sink; already async inside nats.c, but we need flush before stats check)
- `test_manifold_amqp_sink.cpp` (same)
- `test_manifold_broker_source.cpp`
- `test_manifold_session_hmac.cpp` (likely no changes — no dispatch)
- `test_manifold_codecs.cpp` (likely no changes)
- `test_execution_context_channels.cpp` — the `ExecutionContextChannels::AttachingChannelsInstallsTee` case that the prior attempt mishandled deserves special care.
- `test_mcp_request_channels.cpp` / `test_mcp_server.cpp` (SseIn subset)
- `test_evolution_channels.cpp`
- `test_evolvelogger_absorption.cpp`

**Acceptance:**
- `scripts/test.sh all` green with async dispatch enabled (no xfail suite any more).
- TSan clean.
- No `sleep()` calls added as a "wait for delivery" — any such smell gets rejected and routed through `flush_for_tests()` instead.

### Phase 5a.5 — Producer-keyed channel registry (v2.8.5)

**Goal:** implement §24.2.

**Changes:**
1. Add `absl::flat_hash_map<std::string, ProducerStats>` to `DefaultChannelService`, guarded by a `std::mutex` (not absl — keep it simple and lock-light; producer path is already bottlenecked on RBAC's existing mutex).
   ```cpp
   struct ProducerStats {
       uint64_t published_count = 0;
       uint64_t last_published_ns = 0;  // from injected IClock
       // route_count and subscriber_count are derived on-demand from routes_
   };
   ```
2. Update `publish()` to `++producer_stats_[msg.channel].published_count` and stamp `last_published_ns = clock_->now_ns()`. One lock acquisition per publish; keep it on the existing `mu_` where feasible or profile later if it shows up hot.
3. Implement three new TIL words: `channel-producer-list`, `channel-producer-stats`, `channel-producers-by-pattern`.
4. Add unit tests: `test_manifold_producer_registry.cpp` — 6-8 tests covering concurrent updates, pattern filter, `last_published_ns` monotonicity under `ManualClock`, empty-registry behavior.

**Acceptance:**
- All new tests pass, TSan clean.
- Existing tests still pass.

### Phase 5a.6 — Extended cycle / queue / exception stats (v2.8.6)

**Goal:** the four new `CycleStats` counters and the TIL surface that reads them.

**Changes:**
1. Wire `dispatcher_->stats()` into `DefaultChannelService::cycle_stats()`.
2. Extend `channel-cycle-stats` TIL word to emit the new keys (`subscriber-queue-depth`, `dropped-by-overflow`, `dispatcher-exceptions`, `dispatcher-idle-transitions`).
3. Dedicated tests for each counter:
   - queue-depth: use `PausableDispatcher`, publish N, assert depth = N.
   - dropped-by-overflow: RouteSpec with `RingBuffered` + capacity=4, publish 10, assert 6 drops.
   - dispatcher-exceptions: `ExceptionInjectingSink` on 3rd call, publish 5, assert counter = 1.
   - idle-transitions: publish-flush-publish-flush, assert counter increases.

**Acceptance:**
- `channel-cycle-stats .` in interactive TIL shows all eight keys.
- Tests pass, TSan clean.

### Phase 5a.7 — Overflow policy wiring to RouteSpec modes (v2.8.7)

**Goal:** the subscriber dispatcher queue respects per-route `DeliveryMode`.

**Changes:**
1. `ThreadDispatcher::enqueue` consults `DeliveryItem::spec.delivery_mode` and `spec.buffer_capacity`:
   - `RingBuffered` — if per-route queue depth ≥ capacity, drop oldest and increment `dropped_by_overflow`.
   - `DropOldest` — identical to RingBuffered for this purpose (may converge in a later cleanup).
   - `Block` — publisher-thread blocks until capacity frees (publishers get back-pressure). Documented as a foot-gun: MCP servers using Block risk the exact deadlock Phase 5a is trying to fix.
   - `Inline` — bypass the dispatcher, run synchronously (used by hard-wired audit channels per §14).
2. The per-route queue is a wrapper around a shared global dispatcher queue, not a separate thread per route. Book-keeping: each route tracks its own in-flight count + depth atomically.
3. Tests for each mode under synthetic load.

**Acceptance:**
- Four delivery-mode tests pass.
- Hard-wired audit channels bypass the dispatcher (verified by a dedicated test that asserts audit publish delivers on caller's stack under `PausableDispatcher`).

### Phase 5a.8 — etil-web preparation hooks (v2.8.8, no shipped code yet)

**Goal:** make sure the Phase 5a.3 interface would support a Web-Worker backed implementation later.

**Changes:**
1. Audit `IDispatcher` for anything assuming POSIX threads (it shouldn't — the interface is pure).
2. Ensure `DeliveryItem` serializes cleanly (no raw pointers, no thread-specific state). Add a unit test `DeliveryItemIsStructuredCloneable` that round-trips through JSON and asserts equality.
3. Add a stub `WebWorkerDispatcher` in `src/manifold/web_worker_dispatcher.cpp` (gated by `ETIL_WASM_TARGET`), with a single `UNIMPLEMENTED()` body that compiles but throws. Purpose: lock in the interface contract ahead of the actual etil-web work.
4. Add a CI or local-build target `wasm-build` that verifies the code compiles with `ETIL_WASM_TARGET=ON` even if we don't run the web tests (out of scope this sprint).

**Acceptance:**
- `DeliveryItemIsStructuredCloneable` passes.
- WASM build succeeds (as far as Manifold code goes — upstream deps may not all build for WASM and that's a separate concern).

### Phase 5a.9 — Integration tests (v2.8.9, no version bump — integration)

**Goal:** system-level properties that a single unit test can't easily cover.

**New test file:** `tests/unit/test_manifold_phase5a_integration.cpp`.

**Tests:**
1. **McpSubscriberDoesNotBlockInterpretResponse** — simulated MCP interpret-like flow with a TIL-subscriber-shaped sink on `etil.mcp.**`. Publisher finishes in bounded time even when subscriber takes 500ms per call. This is the precise deadlock the sprint is fixing.
2. **ParallelSubscribersHandle10kLoad** — 8 publishers, 16 subscribers (mix of fast and slow), 10k messages. All delivered, no TSan complaints, runs in < 30s on CI.
3. **LongRunningSessionProducesChannelRegistry** — simulate 30s of `etil.mcp.request.**` publishes under `ManualClock`; assert `channel-producer-list` returns every distinct channel; `channel-producer-stats` counts match.
4. **GracefulShutdownUnderLoad** — 8 publishers going hot; service destructor called; no crash, no hang, no TSan violation.
5. **ReentranceFromTransform** — a transform that publishes onto a different channel when it sees certain messages. Publish a message that triggers the transform; assert the secondary publish delivers; no deadlock.

**Acceptance:**
- All 5 tests pass, TSan clean, wall time < 60s total.

### Phase 5a.10 — Documentation + E2E refresh (v2.8.10, no version bump)

**Goal:** the docs reflect what shipped; the live E2E against NATS verifies the new semantics end to end.

**Changes:**
1. README.md Appendix V: add the three new TIL words, update the introspection section, add a "Subscriber dispatch" subsection explaining async semantics + `channel-cycle-stats` new keys.
2. `docs/claude-design/20260418B` §24: change status header from "discovered, pending Phase 5a" to "shipped 2026-04-XX in v2.8.10". Add a "post-implementation notes" subsection capturing anything the plan got wrong.
3. `help.til`: new entries for the three new TIL words.
4. `CLAUDE.md`: update the Manifold section with the async-dispatch note.
5. E2E refresh: write `tests/til/manifold-e2e/reentrant_mcp_subscriber.til` that subscribes to `etil.mcp.**` and confirms no hang. Run via `etil-tui --log --logdir /tmp --exec` on the CI host with v2.8.10 deployed.
6. Add `docs/claude-knowledge/future-list.md` removal: clear the Phase 5a entry (but keep a pointer to the post-mortem).

**Acceptance:**
- All docs current. E2E passes. `help.til` entries visible in TUI.

### Phase 5a.11 — Super-push + GitHub push (operational)

1. `ETIL_GIT_REMOTE=<ci-remote> scripts/super-push.sh --message "Phase 5a — async subscriber dispatch + producer-keyed channel registry"`.
2. Wait for CI pipeline green (full build + test + deploy).
3. Verify E2E on the CI host (the new reentrant-subscriber TIL fixture).
4. `scripts/github-push.sh`.

---

## 5. Testing strategy — the "rich surface"

This is the hill the sprint dies on. The prior attempt was rolled back because a race was masked by stderr prints; the plan must prevent the recurrence systemically.

### 5.1 Test primitives that every Phase 5a test can reach for

All in `include/etil/manifold/test_sinks.hpp` + `include/etil/manifold/test_dispatcher.hpp`:

- `SubscriberCountingSink` — atomic counter.
- `CaptureSink` — existing, unchanged. Still the go-to for content assertions.
- `DelayingSink` — sleeps for configured ms in `accept()`.
- `BlockingSink` — blocks on a test-controlled semaphore.
- `ExceptionInjectingSink` — throws on Nth call.
- `RecordingSink` — records the thread ID of every `accept()` call (used to prove subscriber runs on dispatcher thread, not publisher).
- `InlineDispatcher` — synchronous dispatch (the "test mode" for tests that don't need to exercise async).
- `PausableDispatcher` — ThreadDispatcher wrapper with pause/resume hooks.
- `ManualClock` — `now_ns()` returns whatever the test last `set_time(uint64_t)`'d.

### 5.2 Anti-patterns this sprint explicitly bans

From the prior attempt's lessons:

- **No `std::this_thread::sleep_for(...)` in a test as a "wait for delivery"**. Use `flush_for_tests()`. If the timing property you're testing genuinely can't be expressed as a flush, the test belongs in the integration suite (§5.4).
- **No `std::fprintf(stderr, ...)` / `std::cout` in production code for diagnostic purposes**, even temporarily. The no-direct-stdio policy remains absolute. If a dispatcher step needs to be observed, add a test hook, not a print.
- **No tests that pass more often than they fail but aren't 100% reliable**. Every test runs 3× in CI; if any iteration flakes, the test is broken.
- **No `TEST(X, Y)` without at least one explicit assertion** (prevents "the test ran and didn't crash" false positives).

### 5.3 CI gates

1. **Debug + ASan build** — always on.
2. **Debug + TSan build** (new) — mandatory for Phase 5a. `check-tsan` CMake target wires `-fsanitize=thread` and runs every `test_manifold_*` + `test_execution_context_channels`. Fail on any report.
3. **Debug + UBSan build** — already present; keep it running.
4. **Release build** — always on.
5. **Each new test runs 3× via `ctest --repeat until-pass:1 --repeat after-timeout:1`** — catches flakes at CI time rather than in production use.

### 5.4 Unit vs integration boundary

**Unit tests (Phase 5a.2 / 5a.5 / 5a.6):** test ONE component (`ThreadDispatcher`, producer registry, cycle stats) in isolation. `InlineDispatcher` or mocks for collaborators. Bounded time (< 100ms per test).

**Integration tests (Phase 5a.9):** test `DefaultChannelService` end-to-end with the production dispatcher. Mix of fast and slow subscribers, concurrent publishers, realistic traffic shapes. Bounded time (< 10s per test).

**E2E tests (Phase 5a.10):** `etil-tui --exec foo.til` against the live the CI host deployment with NATS. Manual or CI-gated, not in the unit suite.

### 5.5 Property-based / exhaustive coverage where tractable

For the `IDispatcher` interface specifically:
- **State machine model:** dispatcher states are `Idle`, `InFlight`, `Shutdown`. A fuzz-style test (`TEST(ThreadDispatcher, StateMachineIsClosed)`) walks the state graph with random publish/flush/shutdown invocations and verifies no invalid transitions.
- **Linearizability check (if time permits):** a reduced-cardinality model of the queue under random interleavings, checked against a sequential specification. Overkill for a first pass; note as future-list item.

---

## 6. Refactoring mandate for existing code

Beyond the testability seams listed in §3.3, the sprint also commits to these existing-code cleanups — they're prerequisites for the async refactor being clean, not separate initiatives:

1. **`DefaultChannelService` → smaller pieces.** Current file is 427 lines and growing. After the refactor:
   - Public API class stays in `default_service.cpp` (~200 lines).
   - `deliver_to_route` + layer-3 echo logic → `src/manifold/dispatch_delivery.cpp`.
   - `ThreadDispatcher` → `src/manifold/thread_dispatcher.cpp` (new).
   - Producer registry → `src/manifold/producer_registry.cpp` (new).
   - Audit emission helpers → `src/manifold/audit_helpers.cpp` (new).

2. **`publish_audit_*` helpers lose their `dispatch()` calls inside `dispatch()`**. Currently audit emitters call `dispatch(m, outcome)` with a throwaway outcome, which will now enqueue onto the dispatcher — fine for most paths, but the echo-dropped audit publishes from *within the dispatcher thread's deliver path*, which means we're re-entering `publish()` from the dispatcher. Explicit audit-enqueue contract documented in code comments.

3. **`MessageOrigin` stamp in `publish()` — make it testable.** Currently uses `current_origin()` which reads global state. Refactor to take an `IClock` (for timestamp) and an origin-provider object. Tests inject a fixed origin.

4. **`Message::fresh()` audit factory** — currently sets channel but not origin. Deprecate in favor of `Message::fresh(const std::string& channel, const MessageOrigin& origin)` so audit messages have explicit origin lineage.

5. **`CycleStats` snapshot API** — currently returns by-value. Add `cycle_stats_atomic_snapshot()` that takes an atomic read under the service lock so test assertions don't race the counters.

6. **TestCaptureSink** — add `captured_thread_ids()` accessor so `RecordingSink` isn't needed as a separate class. Bundle.

---

## 7. Concurrency invariants + proof sketches

For each invariant listed in §4 Phase 5a.3, a proof sketch and a test that exercises it:

| Invariant | Proof sketch | Test |
|---|---|---|
| I1: one enqueue per matched route | `dispatch()` holds `mu_` while matching; under `mu_` enqueues to queue under `q_mu_` | 5a.9.ParallelSubscribersHandle10kLoad + counting |
| I2: flush waits for preceding enqueues | `flush()` captures `enqueued_seq` at entry; predicate on `drained_cv_` waits for `delivered_seq >= captured` | 5a.2.FlushForTestsBlocksUntilDispatchedItemsDelivered |
| I3: shutdown drains | `stop_` set → dispatcher continues draining until queue empty, then exits loop | 5a.2.ShutdownDrainsPendingDeliveries |
| I4: origin identity across threads | Message is copied into DeliveryItem under `mu_`; origin is part of Message; no post-enqueue mutation | 5a.2.OriginTupleIdenticalAcrossPublisherAndDispatcher |
| I5: dispatcher atomic counter | `dispatcher_exceptions_.fetch_add(1)` in catch block on dispatcher thread | 5a.6.dispatcher-exceptions test |
| I6: FIFO within a channel | Single global queue; dispatcher pops FIFO; per-channel order preserved | 5a.9 ordering assertion |

Any invariant that can't be matched to a test is a red flag — the invariant is wrong, the test is missing, or the invariant is untestable (which means it's unverifiable and shouldn't be relied on).

---

## 8. Risks + mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| 8.1 Test-first means a long "red" period | medium | Gate the xfail suite behind a flag so main CI stays green; only the Phase 5a branch sees the flag flipped. |
| 8.2 `BlockingSink` + shutdown interact badly | high | Deliberate decision: during `shutdown_dispatcher()`, wait up to `N` ms (configurable, default 1s) for drain. After timeout, detach the thread and leak it; log a WARN to `etil.manifold`. Documented as acceptable for test processes; production should never hit this. |
| 8.3 TSan false positives on absl containers | low | Already suppressed in existing suite; inherit the suppressions file. |
| 8.4 Per-route queue tracking is expensive | medium | Single global queue + per-route atomic counters. No per-route mutex. Benchmark before shipping (add `bench_manifold_dispatcher.cpp` in 5a.9 if needed). |
| 8.5 `PausableDispatcher` drift vs `ThreadDispatcher` | medium | `PausableDispatcher` is a wrapper, not a reimplementation — forwards every call. |
| 8.6 Existing-test reconciliation (5a.4) finds deep semantic issues | medium | Plan §4 Phase 5a.4 bucket C is the escape hatch. If more than 5 tests need rewrite, pause the sprint and re-scope. |
| 8.7 `ExecutionContext::out_` tee → `etil.repl.stdout` publishes from inside TIL running on the dispatcher thread (if a subscriber re-enters TIL) | high | `out_` tee path is publisher-side (the TIL write triggers publish); the tee itself doesn't run on the dispatcher thread. Subscriber-side TIL that writes to `cr` enqueues another publish onto the dispatcher's queue — normal re-entrance handled by I6 + FIFO queue. Test: 5a.2.ReentrantPublishFromSubscriberDoesNotDeadlock covers it. |
| 8.8 Nats/Amqp sink internal queues double-buffer | low | `accept()` on those sinks is already non-blocking enqueue into the broker client's own queue. Dispatcher just calls `accept()` and moves on. |

---

## 9. Cross-platform (etil-web) prep detail

§24.1.2 of doc B has the table. The plan commits these etil-web deliverables in this sprint:

1. `IDispatcher` interface has no POSIX-specific types (no `pthread_t`, no `std::thread` in the interface header; the impl class holds the thread).
2. `DeliveryItem` is trivially copyable or at worst holds types that serialize cleanly (Message uses `std::string` payloads and `absl::flat_hash_map<std::string, std::string>` tags; both serialize fine).
3. The `WebWorkerDispatcher` stub (§4 Phase 5a.8) locks the contract even though the implementation is deferred.
4. The compile-time test `wasm-build` target verifies etil-web compatibility at the header level.

Full Web-Worker implementation is Phase 5b.

---

## 10. Open questions (decide during implementation)

- **Q-10.1 — `BlockingSink` + shutdown timeout policy.** Current plan: 1s timeout, detach thread, log WARN. Alternative: hang the shutdown forever (production-matching), require tests to explicitly unblock their `BlockingSink` before service destruction. Decide in 5a.3. **Tentative: timeout + log.**
- **Q-10.2 — Single global dispatcher queue vs per-route queue.** Plan §4 Phase 5a.7 assumes single queue with per-route depth tracking. Alternative: per-route queue for better isolation. Start with single-queue; measure in 5a.9 bench; switch to per-route if contention hurts.
- **Q-10.3 — Should `Inline` delivery mode bypass dispatcher entirely, or just run the dispatcher's loop synchronously on caller thread?** Hard-wired audit channels NEED Inline for visibility during shutdown. Plan assumes bypass. Confirm during 5a.7.
- **Q-10.4 — Clock injection via service ctor or thread_local?** Plan assumes ctor. Tradeoff: ctor is cleaner but means every DefaultChannelService instance with a non-default clock needs explicit setup. Fine for Phase 5a scope.
- **Q-10.5 — `channel-producer-list` pattern-matching semantics.** Exact match only? Or use same `channel_matches(pattern, name)` as routes? Plan says the latter (routes-style). Confirm in 5a.5.
- **Q-10.6 — `producer_stats_` lock granularity.** Plan assumes reuse of existing `mu_`. If contention shows up in 5a.9 bench, split into its own mutex.

---

## 11. Version progression + commit map

| Phase | Version | Type | Summary |
|---|---|---|---|
| branch create | v2.8.0 | minor | `branch.sh manifold-phase-5a` (done) |
| 5a.1 | v2.8.1 | patch | Seam extraction (IClock, test sinks, flush_for_tests no-op) |
| 5a.2 | v2.8.2 | patch | Async-correctness xfail test suite |
| 5a.3 | v2.8.3 | patch | ThreadDispatcher implementation — xfail flag flipped on |
| 5a.4 | v2.8.4 | patch | Existing-test reconciliation (flush_for_tests everywhere) |
| 5a.5 | v2.8.5 | patch | Producer-keyed channel registry + 3 new TIL words |
| 5a.6 | v2.8.6 | patch | Extended cycle/queue/exception stats |
| 5a.7 | v2.8.7 | patch | Overflow policy wiring to RouteSpec modes |
| 5a.8 | v2.8.8 | patch | etil-web prep (WebWorkerDispatcher stub, wasm-build target) |
| 5a.9 | v2.8.9 | patch | Integration tests (including the MCP deadlock repro) |
| 5a.10 | v2.8.10 | patch | Docs + E2E refresh + help.til entries |
| 5a.11 | v2.8.10 | operational | super-push + github-push |

Phase-by-phase the feature branch ships 10 commits (plus branch-create).

---

## 12. Checklist (to copy into commit message of each phase)

```
[ ] scripts/version-bump.sh patch
[ ] Implementation / test changes staged
[ ] scripts/build.sh all  — clean
[ ] scripts/test.sh all   — clean
[ ] check-tsan  — clean
[ ] scripts/check-logging-policy.sh  — clean
[ ] No sleep() in any new test
[ ] No direct stderr/cout in any new prod code
[ ] Existing tests still pass (5a.4+ only)
[ ] Commit message names the invariants the phase proves
```

---

## 13. Out of scope, next after Phase 5a

Promoted from doc B §24 to the Phase 5b / 6 scope:
- Universal codec boundary for subscriber delivery (§24.1.3 option (a)).
- etil-web Web-Worker dispatcher implementation.
- Per-route subscriber queue capacity as a TIL-configurable knob.
- Benchmark suite (`bench_manifold_dispatcher.cpp`) run in CI.
- Producer registry GC policy (right now it grows monotonically with distinct channel names — acceptable until a process emits millions of channel names).
