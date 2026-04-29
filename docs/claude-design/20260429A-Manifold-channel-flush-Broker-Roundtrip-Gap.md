# Manifold `channel-flush` + Async Pipeline Tick-Budget Gap

**Date:** 2026-04-29
**Status:** Gap analysis; recommendations; not a plan
**Audience:** Manifold authors / ETIL server authors
**Related:**
- `20260418B-IO-Channel-Pipeline-Architecture.md`
- `20260420A-Manifold-Phase-5a-Implementation-Plan.md`
- `20260426C-Manifold-E2E-Validation-Plan.md` (§9 Risks — predicted this)
- (forthcoming) `20260429B-Manifold-E2E-Validation-Results.md`

---

## 1. Why this document exists

While running the Manifold E2E gate (`tests/e2e/manifold/broker_loopback_nats.til`) against the deployed server, **`test-broker-ordering` reproducibly captures 2 of 3 messages**, leaving `seen3` empty. `test-broker-single` (one message) passes. The single-message variant survives the round-trip; the three-message variant does not.

The validation plan §9 anticipated this:

> **Hot-stream timing:** the documented "install reader before writer" invariant requires the harness to issue the TIL forms in the right order within a single `interpret` call. If the deployed server's dispatcher introduces enough latency that even same-call ordering is racy, `channel-flush` semantics need re-examination before the gate can open.

This doc is that re-examination.

---

## 2. Concrete symptom

```
test-broker-single:    PASS                # one-message round-trip succeeds
test-broker-ordering:  PASS                # seen1 == "first"  → first message round-trips
                       PASS                # seen2 == "second" → second round-trips
                       FAIL                # seen3 != "third"  → third never arrives in time
```

The first two messages of three round-trip correctly through the deployed NATS broker; the third does not arrive before the test finishes its synchronization step. Because `seen3` is initialised to `s" "` (empty), the final `expect-streq` against `s" third"` fails.

There is a separate, smaller harness-side issue: `tests/e2e/manifold/harness/run_e2e.py` only counts `<name>: PASS|FAIL` markers and misses bare `PASS` / `FAIL` lines (the original bash harness counted both with `grep -c`), so this run was incorrectly reported as exit 0. That bug is real but out of scope here — track it with the harness; this document is about the underlying flush semantics.

---

## 3. What `channel-flush` actually does

### 3.1 Call chain

```
TIL: channel-flush
  └── prim_channel_flush                        src/manifold/til_primitives.cpp:922
        └── ChannelService::flush_for_tests()   include/etil/manifold/service.hpp:161
              └── DefaultChannelService::flush_for_tests() override
                    src/manifold/default_service.cpp:146
                      └── ThreadDispatcher::flush()
                            src/manifold/dispatcher.cpp:143
```

### 3.2 `ThreadDispatcher::flush()` — the actual wait

```cpp
void flush() override {
    std::unique_lock<std::mutex> lk(st_->mu);
    st_->drained.wait(lk, [&] {
        return st_->q.empty() &&
               !st_->in_flight.load(std::memory_order_acquire);
    });
}
```

Two conditions, both required, before the wait returns:

1. `q.empty()` — the dispatcher's internal queue has no enqueued deliveries.
2. `!in_flight` — the worker thread is not currently inside a sink callback.

When both hold, every publish issued **before** the call has been **handed off to its sinks** (`Sink::accept(msg)` has returned). For inline-deliverable sinks (in-process subscriptions writing into local state) that means delivery is complete by the time `flush_for_tests()` returns. Per the doc-comment on the base virtual:

> Default implementation is a no-op (sync implementations such as the current
> DefaultChannelService deliver on the caller's stack, so there is nothing to
> flush). The Phase 5a ThreadDispatcher override will actually block on the
> dispatcher queue.

### 3.3 What "delivery" means for a broker sink

The `NatsSink::accept(msg)` path is **not** "the message is on the broker" — it is "the message has been handed to the nats-c client library." From `src/manifold/broker_sink.cpp:74-125`:

```
publish_wire(subject, body, headers)   // → nats-c natsConnection_Publish
forwarded_.fetch_add(1, ...)
return                                  // ← accept() returns; in_flight clears
```

`natsConnection_Publish` is asynchronous. The message lives in nats-c's outgoing buffer, gets sent over TCP to the broker on the c-client's I/O thread, the broker fans it out to subscribers, the *source*-side nats-c instance receives it, hands the bytes back into Manifold via `channel-source-nats`'s callback, and that callback re-enters `ChannelService::publish` on the source-side channel — at which point a *second* round through the local dispatcher occurs.

`flush_for_tests()` returns after the **first** local dispatch completes. The remaining four legs of the round-trip — TCP→broker→TCP→source publish→local dispatch — are entirely outside its visibility.

The doc-comment is accurate **for in-process subscribers**. For broker-bridged channels it is technically correct ("the local dispatcher is drained") but operationally insufficient ("the message you just wrote is observable on the receiving end"), because no part of the round-trip past `Sink::accept()` is gated.

---

## 4. The async pipeline tick-budget issue

### 4.1 How `obs-subscribe` actually drives delivery

Once `channel-flush` returns, the test executes:

```forth
3 obs-take
['] capture-ordered obs-subscribe
```

`obs-subscribe` (at `src/core/observable_primitives.cpp:303`) decides between sync and async drains:

```cpp
if (needs_async(src)) {
    AsyncPipeline pipeline;
    execute_pipeline(src, ctx, observer, &pipeline);
    ok = run_async_pipeline(ctx, pipeline);
} else {
    ok = execute_pipeline(src, ctx, observer);
}
```

ChannelSubscription-rooted pipelines are async. So the subscription drain enters `run_async_pipeline`:

```cpp
// src/core/observable_async.cpp:14
bool run_async_pipeline(ExecutionContext& ctx, AsyncPipeline& pipeline) {
    auto* uv = ctx.uv_session();
    if (!uv) return false;
    auto* loop = uv->loop();
    pipeline.register_handles(loop);

    bool ok = true;
    while (!pipeline.complete()) {
        uv_run(loop, UV_RUN_NOWAIT);
        pipeline.check_deferred();
        if (!ctx.tick()) {            // ← execution-limit budget
            ok = false;
            pipeline.stop_all();
            break;
        }
        if (pipeline.complete()) break;
        if (pipeline.stopped() && !pipeline.has_pending_deferred()) break;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    pipeline.close_all();
    uv_run(loop, UV_RUN_NOWAIT);
    return ok;
}
```

### 4.2 Why this terminates the third message

`ctx.tick()` is the per-interpret execution-limit checker. The MCP `interpret` tool sets `ctx.set_limits(...)` from the role's `interpret_execution_limit` per `src/mcp/tool_handlers.cpp` around line 488. Each iteration of the polling loop above costs **one tick**.

At a 50 µs sleep between iterations, the loop runs ~20,000 iterations per second. If the per-interpret tick budget is, e.g., 100,000, the polling loop terminates after ~5 seconds — independent of whether broker round-trips are still in flight.

For one message, the broker round-trip latency fits comfortably under the budget. For three serial round-trips, the budget exhausts first. The `pipeline.stop_all()` path then returns control to the test, which proceeds to the `expect-streq` calls with `seen3` still empty.

### 4.3 The structural mismatch

`ctx.tick()` exists to bound runaway *user code* — to stop a `: forever begin again ;` from blocking the interpreter. An event-loop poll that is **idling on I/O** is categorically different: it is not consuming CPU on user computation; it is waiting for a network event. Charging tick budget against an idle wait conflates two unrelated concerns.

The 50 µs sleep is itself a poll-don't-block compromise (the libuv loop is run with `UV_RUN_NOWAIT`). Replacing it with `UV_RUN_ONCE` plus appropriate cancel/timeout handles would let the loop sleep on the kernel until I/O arrives, eliminating both the tick consumption and the 50 µs latency floor — but that is a larger refactor.

---

## 5. Why `test-broker-single` passes and `test-broker-ordering` does not

| | `broker-single` | `broker-ordering` |
|---|---|---|
| Messages published | 1 | 3 |
| Local dispatch hops | 1 | 3 |
| NATS round-trips serially required | 1 | 3 |
| `obs-take(N)` waits for | 1 item | 3 items |
| Tick budget consumed before completion | small | exceeds limit |

Single-message latency is dominated by the broker round-trip itself (sub-millisecond on a co-located NATS container). Three-message ordering verification needs the polling loop to wait for three round-trips to land — three windows of waiting, each consuming ticks at 20,000/sec — which exceeds the budget the `interpret` tool grants.

---

## 6. Options to close the gap

Three families, in increasing scope and decreasing localness.

### Option 1a — Skip ticks during async-pipeline I/O wait (recommended)

**Change:** `run_async_pipeline()` does not call `ctx.tick()` while it is in the "waiting for I/O" portion of its loop. Either:

- (a) Replace the unconditional `ctx.tick()` with a much larger, separate budget specifically for async-pipeline polling (e.g., 10× the normal interpret budget, or a wall-clock budget instead).
- (b) Skip `ctx.tick()` when no observer callback was invoked since the previous iteration (i.e., the loop is purely waiting, not delivering).

**Diff scope:** `src/core/observable_async.cpp` (~30 LOC), `include/etil/core/observable_async.hpp` if a public knob is added. No changes to the dispatcher, no changes to broker sinks, no changes to TIL primitives.

**Risk:** Loses some defense against runaway user code that escapes through an async pipeline. Mitigation: keep an absolute wall-clock cap (e.g., 30 s per `interpret` call), separate from tick budget. Wall-clock caps are already a likely good idea — `interpret_execution_limit` was designed for the synchronous case; the async case wants a different bound.

**Strength:** The diagnosis says the pipeline's wait *is correct* — it would deliver the third message if it weren't interrupted. The fix matches the bug.

### Option 1b — Broker sinks become flushable; `channel-flush` flushes them too

**Change:** `BrokerSinkBase::flush()` (currently a no-op stub at `include/etil/manifold/broker_sink.hpp:72`) calls `natsConnection_Flush()` (or equivalent for AMQP), blocking until the broker has acknowledged all pending publishes from this client. `ChannelService::flush_for_tests()` iterates registered sinks and invokes `flush()` on each before returning.

**Closes:** the local-publish → broker leg.
**Does NOT close:** the broker → source-subscriber → local-deliver leg. The receiving side has no equivalent "I have processed every message I was going to process from you" handshake (NATS subscriptions are continuous; there is no terminator).

**Diff scope:** `src/manifold/nats_sink.cpp`, `src/manifold/amqp_sink.cpp` (override `flush()`), `src/manifold/default_service.cpp` (iterate sinks in `flush_for_tests`).

**Risk:** Half a fix. Without source-side synchronization, race remains for any test that needs the source to have received and re-published before assertions run. For the present failure mode (Option 1a's diagnosis is correct), 1b is also necessary if 1a alone leaves a gap, but 1a addresses the actual bug.

### Option 1c — New TIL primitive: `channel-await ( pattern n duration-us -- bool )`

**Change:** Add a TIL primitive that blocks until `n` messages have been observed on local channels matching `pattern`, or `duration-us` elapses. Returns `true` on success, `false` on timeout. Implementation: dispatcher tracks per-channel delivery counts (Phase 5a.5 producer-keyed registry already exists for `producer_stats(channel)` per `service.hpp:173` — extend); the primitive sleeps on a CV that the dispatcher signals when a delivery count crosses a registered threshold.

**Closes:** end-to-end synchronization, by an explicit programming model.

**Diff scope:** larger — new TIL word, dispatcher signaling, await registry. Touches the public Manifold surface (one new word) and the dispatcher's notify path.

**Risk:** Adds public API. Requires deciding on the right pattern semantics. But **this is the synchronization primitive validation plan §9 implies is needed** for any test more complex than fire-and-forget. If we end up writing a meaningful E2E suite, we will need this regardless.

### Option 1d — Pragmatic: open the gate on `broker-single` only; defer ordering

**Change:** Drop `test-broker-ordering` from the gate. Open the gate on the evidence we already have:
- 6/6 local-loopback tests PASS (validates the in-process dispatcher under sequential and concurrent load — including the 100-message burst).
- `test-broker-single` PASSes (validates the broker round-trip end-to-end for a single message).

**Argument:** Manifold's broker bridge is what's being validated — the bridge is proven by *any* successful round-trip. Multi-message ordering is a NATS-level guarantee (per-publisher per-subject), not a Manifold-level one; testing it through Manifold mostly tests Manifold's wait machinery, which is exactly what the present analysis shows is the actual deficiency. Document the multi-message wait as a known limitation, file 1a (and possibly 1c) as follow-ups, proceed to ITC.

**Risk:** Lets a real wait-semantics deficiency ride. Mitigation: file the follow-up immediately and tie it to the next Manifold-touching merge. A documented known issue with a tracked follow-up is better than a non-deterministic test that occasionally passes.

---

## 7. Recommendation

In priority order:

1. **(1a) Tick-budget fix** — small, correct, addresses the actual bug. Should land regardless of whether the gate is opened on broker-single only.

2. **(1d) Open the gate on broker-single + 6 local tests** — gets ITC unblocked today. Pair with (1a) as a tracked follow-up.

3. **(1c) `channel-await` primitive** — defer to next Manifold sprint. Will become necessary if the E2E suite grows beyond round-trip smoke; `broker-ordering` is just the first test that needs it.

4. **(1b) Sink flush** — defer indefinitely. Useful eventually but does not close the receive leg, which is the harder half. Do not invest until receive-side synchronization is also designed.

The pairing of (1a) + (1d) is the recommended path: ship (1a) on its own short feature branch; declare the gate open on the validated evidence; file `channel-await` as a planned Phase-5b extension.

---

## 8. Cross-references

- `tests/e2e/manifold/local_loopback.til` — 6/6 passing; not affected by this gap (in-process subscriptions deliver synchronously through the local dispatcher and `flush_for_tests()` is sufficient).
- `tests/e2e/manifold/broker_loopback_nats.til` — `test-broker-single` passes; `test-broker-ordering` is the canonical reproducer for the gap.
- `src/core/observable_async.cpp:14-40` — `run_async_pipeline`, the tick-budget consumer.
- `src/manifold/dispatcher.cpp:143-149` — `ThreadDispatcher::flush()`, the local-only wait.
- `src/manifold/broker_sink.cpp:74-125` — `BrokerSinkBase::accept()`, the local→broker handoff that is invisible to the local flush.
- `include/etil/manifold/broker_sink.hpp:72` — the no-op `flush()` stub Option 1b would override.
- `tests/e2e/manifold/harness/run_e2e.py` — contains a separate counting bug (only matches `<name>: PASS|FAIL`, misses bare markers); track that with the harness, not here.
