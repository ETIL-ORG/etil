# Cross-Session Services — Architecture Design Input

**Date:** 2026-04-26
**Status:** Design Input (no implementation plan yet)
**Audience:** ETIL server / Manifold authors
**Related:**
- `20260418B-IO-Channel-Pipeline-Architecture.md` (Manifold)
- `20260420A-Manifold-Phase-5a-Implementation-Plan.md` (async dispatch)
- `20260426A-Manifold-Cross-Session-ITC-Survey.md` (gap analysis)

---

## 1. Use case

Run **long-duration ETIL services inside MCP sessions**, and let **other MCP sessions on the same server** invoke or stream from those services. Constraints:

- All in-process, in-memory. **No TCP/IP**, no kernel transitions on the hot path.
- **No external broker** (no NATS, no AMQP, no Redis). The point is to serve the in-process case without deploying a message bus.
- Fast. Sub-microsecond intra-process unicast for the hot path is the target class.
- RBAC-aware. Services run under their registrant's permissions; callers must be authorized to call them.
- Compatible with the existing Manifold subsystem and the Phase 5a `ThreadDispatcher`.

This document is a **design input**, not a sprint plan. It captures what the codebase already gives us, what the C++ ecosystem already gives us, and the shape of the smallest end-to-end thing we could build.

---

## 2. What the codebase already provides

Findings sourced from a full read of `src/mcp/`, `src/manifold/`, `src/core/`, and `src/lvfs/`. Citations omitted here for brevity; they live in `20260426A-Manifold-Cross-Session-ITC-Survey.md` and the codebase agent's working notes.

### 2.1 Sessions are already long-lived

- `Session` lives in `McpServer::sessions_` (shared_ptr map) across HTTP requests.
- Default **30-minute idle TTL**, role-overridable via `permissions_ptr->session_idle_timeout_seconds`.
- `cleanup_idle_sessions()` runs on every new request; `destroy_session()` is the explicit kill path.
- Session lifecycle events publish on `etil.mcp.session.{opened,closed}`.
- Anonymous (API-key) and authenticated sessions both supported.

So "a session that hosts a service for hours" is **already a supported lifetime** — we don't need a new "service container" concept; a session is one.

### 2.2 Per-session vs shared state

| Component | Scope |
|---|---|
| `Dictionary` (colon definitions) | per-session |
| `Interpreter` / `ExecutionContext` | per-session |
| Heap allocations (`HeapString`, `HeapMap`, `HeapObservable`, …) | per-session |
| `LVFS` `/home/<sid>/` | per-session writable; `/library/` shared read-only |
| `UvSession` (libuv loop) | per-session |
| `HttpClientState`, `MongoClientState` (wrapper) | per-session |
| MongoDB connection pool | **shared (one per server)** |
| `ChannelService` (Manifold) | **shared (one per server)** |

**Implication:** Every `: my-service ...` definition is invisible across sessions today. Cross-session function call is *not* "look up B's word from A"; it has to be "send a typed message to a service-handle".

### 2.3 Routes outlive the registering request

`channel-add-transform`, `obs-loop-channels`, and the various tap words install routes on the shared `ChannelService`. Those routes survive past the `interpret` call that registered them. **But:**

- They are **orphaned** when the registering session is destroyed (no lifecycle hook).
- They run with **no `RolePermissions` context** when the dispatcher fires the sink.
- They run on the dispatcher worker thread, with **no `ExecutionContext`** — so an xt can't `type cr` to anywhere visible.

These are exactly the three holes that block "service in a session" today.

### 2.4 No `spawn` / `detach` / `service` primitive exists

All TIL execution is synchronous with the request. `obs-subscribe` is terminal and blocks. `obs-timer` runs only inside `run_async_pipeline()` which still blocks the request thread until the pipeline drains.

There is **no precedent** in the codebase for "register this xt to be invoked by future events and return immediately."

### 2.5 No published Manifold throughput baseline AND no TIL-level E2E validation

`tests/unit/test_manifold_integration.cpp::MultiSessionPublishLoadNoDrops` exercises 200 sessions × 50 messages across 8 threads, but reports correctness only — no latency or throughput numbers. Equally important: every existing Manifold test is C++. There is **no TIL-level end-to-end test** that exercises a message round-trip through the channel API, the dispatcher, the broker bridges, and back into TIL via `obs-take` / `obs-subscribe`. Until that exists and passes against the deployed server, Manifold is an unproven substrate from the language's point of view.

---

## 3. What the C++ ecosystem already provides

Findings sourced from a 2026-Q1 web survey of in-process / inter-thread C++ messaging libraries.

### 3.1 Three real candidates

**moodycamel ConcurrentQueue + rigtorp SPSCQueue** (lock-free queues)
- Header-only. BSD/Boost (moodycamel) and MIT (rigtorp).
- moodycamel is the de-facto C++ MPMC since 2014; v1.0.5 released April 2026; bulk ops; bounded and unbounded variants; blocking variant via `LightweightSemaphore`.
- rigtorp SPSCQueue: ~133 ns RTT class for known 1:1 routes.
- Caller pumps; library owns no threads. Lowest integration friction available.

**SObjectizer 5.8.5** (actor framework with mboxes)
- BSD-3-Clause, actively maintained (5.8.5 Nov 2025).
- Single library covers Actor + Pub/Sub (mboxes) + CSP-channels; `so5extra` adds synchronous request/response.
- Closest single-library fit semantically. **But** it owns the dispatcher and has its own agent lifecycle model — retrofitting onto the MCP `Session` is high friction.

**Eclipse iceoryx2 0.8.x** (zero-copy IPC)
- Apache-2.0 / MIT.
- True zero-copy pub/sub. **No central daemon required** (this is the headline change vs. iceoryx classic, which always needed RouDi).
- C++ bindings shipped with v0.6/0.7. Has a `local` service variant for in-process use.
- Pre-1.0 (API churn risk). Rust core means a Rust toolchain in our CMake graph. **Wait for 1.0** before committing.

### 3.2 Dismiss list

- **iceoryx (classic v1)** — single-process mode still requires `PoshRuntimeSingleProcess` (RouDi in-process). Maintainers themselves point to iceoryx2.
- **QP/C++** — GPLv3 / paid commercial dual license. Disqualified.
- **RxCpp** — unmaintained; we already cover observable patterns via Manifold. Zero value-add.
- **CAF (C++ Actor Framework)** — alive (1.1.0 June 2025), but, like SObjectizer, owns the scheduler. Same retrofit problem with a heavier conceptual surface.

### 3.3 What no library will give us

Even after picking the best library, we still own:

- **Named channel registry tied to ETIL session lifetime** — none of these libraries know about MCP sessions or RBAC.
- **RBAC hook on subscribe / publish / unicast** — none ship an authorization callback in the hot path.
- **Request/response correlation on top of pub/sub** — moodycamel and iceoryx2 don't ship this. SObjectizer does, opinionatedly.
- **TIL primitives** (`service-register`, `service-call`, `service-reply`, …) — pure ETIL surface work.
- **Backpressure-policy parity with Manifold** (`RingBuffered` / `DropOldest` / `Block`) — moodycamel and iceoryx2 give us bounded/unbounded; we wrap.

---

## 4. Recommendation: build, on top of moodycamel + rigtorp

The hot path we care about — sub-microsecond intra-session unicast — is *just* a queue push plus a futex wake. moodycamel and rigtorp give us that for free. Everything else (RBAC, named registry, correlation IDs, TIL primitives, Manifold-policy parity) is glue we'd write **no matter which library we picked**, so we may as well skip the framework retrofit.

What to crib from the libraries we're not adopting:

- **SObjectizer mbox API** — named, typed, multi-subscriber. Clean shape for our service registry's request channels.
- **ZeroMQ DEALER/ROUTER** — correlation-ID + per-session reply mailbox + `std::promise`/`std::future` bound at send time. The textbook req/rep-on-pub/sub pattern.
- **Aeron MPSC ring-buffer header** (4-byte length + 4-byte type) — gives us a wire-equivalent in-memory frame format. If we ever need to lift the ITC layer to cross-process via shared memory or to bridge a real broker, no redesign required.

---

## 5. Architectural shape

Two complementary layers, both stacked on the existing Manifold:

### Layer A — Extend Manifold for fan-out / observability across sessions

Closes the gaps identified in `20260426A` §5–6.

| # | Change | Scope |
|---|---|---|
| A1 | `RouteSpec::target_session_id` (optional string) + filter in `dispatch()` | ~50 LOC |
| A2 | `channels_cross_session: bool` role flag, gates A1 + A4 | ~30 LOC |
| A3 | Subscriber-context tee sink: capture subscribing session's `ExecutionContext::out_` at subscribe time; reuse on each delivery so an xt's `type cr` reaches the subscriber's TUI | ~200 LOC |
| A4 | Session-lifecycle hook: `Session::~Session()` removes routes filtered to / owned by this session | ~60 LOC |
| A5 | RBAC propagation to async xt: store `RolePermissions*` on the route; apply at dispatch | ~100 LOC |

**Total Layer A:** ~440 LOC + tests. No breaking changes to existing routes. Behind RBAC. Phase 5a `ThreadDispatcher` is a prerequisite for A3/A5 to be useful at scale.

The TUI `/notify`-lockout case (one TUI publishes, another tries to subscribe via `mcp-on-progress` and the subscriber blocks its own input) is **a demonstration that this layer is missing**, not the target. Solving it falls out of A1+A3+A4 as a side effect; it is not what justifies the work.

### Layer B — New "Service Registry" subsystem for typed request/response

This is what makes "long-duration ETIL services" real.

**Data model:**

```
ServiceEntry {
  name                : string            // unique server-wide
  owner_session_id    : string            // session that registered it
  owner_permissions   : RolePermissions   // captured at register-time
  request_queue       : moodycamel::ConcurrentQueue<ServiceRequest>
  detached            : bool              // outlives owner session?
  last_request_at     : steady_clock::time_point
  created_at          : system_clock::time_point
  in_flight           : atomic<size_t>
  total_handled       : atomic<uint64_t>
}

ServiceRequest {
  request_id          : uint64_t
  caller_session_id   : string
  caller_permissions  : RolePermissions
  reply_channel       : string            // Manifold channel to publish reply on
  payload             : HeapMap           // typed args
  deadline            : optional<steady_clock::time_point>
}
```

The owner's session runs an event loop that pops requests, invokes a registered xt under `owner_permissions`, and publishes the reply on `reply_channel`. Caller awaits via Manifold subscription on `reply_channel` filtered by `request_id`.

**TIL surface (sketch — names to be refined):**

| Word | Stack effect | Notes |
|---|---|---|
| `service-register` | `( name xt -- handle )` | xt is the request handler; handle is integer ID |
| `service-detach` | `( handle -- )` | service outlives owner session; only if role grants `services_detach` |
| `service-unregister` | `( handle -- )` | removes the service; in-flight requests fail |
| `service-call` | `( name args-map -- result-map )` | synchronous; blocks calling session until reply |
| `service-call-async` | `( name args-map -- request-id reply-obs )` | non-blocking; observable yields the reply |
| `service-reply` | `( request-id result-map -- )` | called from inside the handler xt |
| `service-list` | `( -- array-of-maps )` | introspection |
| `service-stats` | `( name -- map )` | counters |

**RBAC:**

- `services_enabled: bool` — master switch.
- `services_register: bool` — may register at all.
- `services_detach: bool` — may detach (service outlives session).
- `services_call_grants: ServiceGrant[]` — pattern + allow/deny, mirrors `channel_grants`.
- Reserved names (`etil.system.*`) bypass register but require `services_admin`.

**Lifetime:**

- Default: dies with owner session. `service-unregister` runs in `Session::~Session()` for all owned services.
- `--detached`: service entry persists; ownership transfers to a server-internal "anchor session" with the captured `owner_permissions`. Admin can `service-unregister` it.

**Hot path:**

- `service-call` allocates the request, pushes onto the owner's `request_queue`, awaits on a per-call `std::promise` resolved when the reply arrives on `reply_channel`. With moodycamel + condvar wake, this is the sub-µs queue-push + futex-wake we want.
- `reply_channel` is a uniquely-named Manifold channel (`etil.service.reply.<owner_sid>.<request_id>`) — we get RBAC, observability, and can introspect via existing `channel-trace`.

**Total Layer B:** ~600–800 LOC + thorough tests + benchmarks.

### Why both layers

- **Layer A alone:** observability and best-effort fan-out across sessions. Good enough for telemetry, audit, debug taps. Not enough for "session A asks session B's service for an answer".
- **Layer B alone:** request/response works, but every notification still has to be modeled as a service call. No streaming events, no observability without polling.
- **Both:** clean separation of concerns. Pub/sub for events. Services for typed RPC. They share the queue substrate (moodycamel) and the RBAC machinery.

---

## 6. Open questions

1. **Performance baseline first.** Before adding either layer we should measure what Manifold publish/dispatch costs today, end-to-end including RBAC. Without that number we cannot claim sub-µs anything.
2. **Cancellation.** Today there's no way to abort a stuck `interpret`. `service-call` will inherit that bug. Do we tackle it as part of Layer B (per-call deadline + cooperative tick checks) or as a separate workstream?
3. **Service request fairness.** If service B's queue is moodycamel MPMC, multiple callers can flood. Do we want per-caller rate limits, priority queues, or both?
4. **Detached service ownership.** A `--detached` service whose owner exits — who can introspect its state? Who can unregister it? Probably an `services_admin` role; needs design.
5. **TIL handler concurrency.** A service's handler xt runs on **the owner session's** event loop. Single-threaded by default. Do we ever want a worker pool per service? Probably not in v1.
6. **Reply marshalling.** `HeapMap` reply works for JSON-shaped data. What about returning a HeapObservable (a stream)? Either we forbid it, or we register a reply-observable channel and return a handle. Decide before TIL surface lands.
7. **Echo / cycle detection across services.** A service that calls another service that calls back — is this a real use case, and does our cycle detection cover it?

---

## 7. Hard prerequisite — Manifold E2E validation

**No ITC code (Layer A or Layer B) lands until Manifold itself is validated end-to-end via TIL tests against the deployed server and the deployed MQ.** Existing C++ unit tests show Manifold is internally consistent; they do not show that the language surface, the dispatcher, the broker bridges, and the deployment topology compose correctly. Building cross-session ITC on an unvalidated substrate is castle-on-sand.

**Three E2E test categories required:**

1. **Local channel round-trip (TIL).** A `.til` test publishes a typed message onto a channel and reads it back via `obs-take` and `obs-subscribe`. Pure in-process. Lives under `tests/til/`. Verifies the TIL surface against the channel service.
2. **Broker round-trip (TIL).** A `.til` test publishes onto a channel that taps the production MQ via `channel-tap-nats` (or `channel-tap-amqp`), then reads back through `channel-source-nats` (or `-amqp`). Verifies wire headers, codecs, RBAC, and the broker integration — the full Phase 3 surface — actually round-trips.
3. **Live-server execution.** Both tests above must run against the **deployed ETIL server** talking to the **deployed MQ**, not a localhost dev build. Driver options: `etil-mcp-client --exec <script>` (TUI in scripted mode) or `curl` against the MCP HTTP endpoint. The harness must initialize the MCP session, send the test TIL via `tools/call interpret`, and assert on the response payload.

**Definition of done for the prerequisite:**
- All three test categories pass on green.
- Test outputs include observed publish→deliver latencies (even rough — coarse `sleep`/timestamp diffs are fine for a first pass; precise benchmarks come later).
- Failures, if any, are filed as pre-ITC bugs and fixed before the gate opens.
- A short results note lands as a follow-up design-knowledge doc (date-stamped).

Until that note exists, this design doc remains **input only** — no implementation plan should be written, no feature branch opened.

## 8. Decision gate (after the prerequisite passes)

1. **Confirm the use case is worth it.** Is "long-duration services in sessions" something we will use repeatedly, or a one-off? If one-off, MongoDB + polling is cheaper.
2. **Pick scope.** Layer A only? Layer B only? Both? Layer B is the structural priority — typed cross-session RPC is what enables the long-duration-services-in-sessions use case at all. Layer A is an enabling refactor (Manifold gains the missing primitives Layer B builds on) and ships first only if the dependency analysis demands it.
3. **Commit to a benchmark.** Add `tests/benchmark/manifold_throughput.cpp` and `tests/benchmark/service_rpc_latency.cpp`. Set numerical targets (e.g., service-call P99 < 50 µs intra-process, Manifold publish > 1M msg/s). The §7 E2E tests give us correctness; benchmarks give us performance numbers.
4. **License audit.** moodycamel ConcurrentQueue (BSD/Boost) and rigtorp SPSCQueue (MIT) are clean. Add to `cmake/Dependencies.cmake` only after legal review (CLAs + LICENSES manifest).
5. **Plan documents.** Once gated, write `20260427X-Cross-Session-Services-Phase-1-Implementation-Plan.md` covering A1+A3+A4 with version-bumping and phase commits per `20260403A-feature-branch-workflow.md`.

---

## 9. Summary

- We can host long-running services in sessions today *as state*; we cannot *invoke* them across sessions.
- The four blockers are: no per-session targeting, no subscriber-context output, no route lifecycle cleanup, no RBAC at xt-fire time. All are fixable inside Manifold (~440 LOC).
- For genuine request/response RPC we need a small new subsystem — a **Service Registry** built on `moodycamel::ConcurrentQueue` for the queue substrate and Manifold channels for the reply path (~600–800 LOC).
- Picking moodycamel + rigtorp + custom glue beats picking SObjectizer or iceoryx2 because the hot path is *just* a queue, the rest is glue we'd write either way, and we avoid retrofitting around someone else's dispatcher.
- **Nothing here ships before Manifold itself is validated end-to-end via TIL tests against the deployed server and the deployed MQ.** The "fast" claim is secondary; the "actually works as advertised" claim is primary.
