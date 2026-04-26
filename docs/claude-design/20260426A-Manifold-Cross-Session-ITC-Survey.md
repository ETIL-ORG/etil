# Manifold Cross-Session ITC — Capability Survey & Gap Analysis

**Date:** 2026-04-26
**Status:** Survey / Design Input
**Audience:** Manifold authors, MCP server authors
**Related:** `20260418B-IO-Channel-Pipeline-Architecture.md`, `20260420A-Manifold-Phase-5a-Implementation-Plan.md`

---

## 1. Motivation

The TUI's new `/notify`, `/notify-file`, `/!`, `/@` commands (etil-tui v1.4.0) send `notifications/progress` from one MCP session and the operator wants another session to receive them. This raised the broader question:

> *Can Manifold serve as the project's secure Inter-Thread Communication (ITC) substrate between MCP sessions, removing the need for an external MQ (NATS / AMQP) for in-process server use?*

This document records what works today, what does not, and the minimum patch required to make session-A → session-B unicast a first-class operation. It does **not** propose code yet; it gates whether to invest in that direction or keep the broker as the substrate.

---

## 2. Scope

In scope:
- Pub/sub between two MCP sessions on the **same** `etil_mcp_server` process.
- RBAC scoping, session-id provenance, and SSE return path.
- Behavior under async dispatch (Phase 5a `ThreadDispatcher`).
- Comparison with the NATS / AMQP broker integration.

Out of scope:
- Multi-process / multi-host fan-out (broker territory).
- Persistence and durable replay.
- TIL surface design for any new API (deferred to a follow-up plan if approved).

---

## 3. Status today — what works

### 3.1 Single shared `ChannelService` per server

`McpServer::McpServer()` constructs **one** `DefaultChannelService` and hands a raw pointer to every per-session `ExecutionContext` via `ctx.set_channels(channels_.get())` in `tool_handlers.cpp:519` (when `tool_interpret` enters TIL). All sessions therefore share a single routing table — the precondition for cross-session traffic.

### 3.2 Pattern matching is name-only, not session-aware

`channel_matches()` in `src/manifold/channel_name.cpp:93` uses pure dotted-name globbing (`*` and `**`). There is **no automatic session_id filter** at any level of the routing path. If session A publishes `etil.mcp.in.progress` and session B subscribes to a pattern that matches, the route will fire. Filtering by `tags["session_id"]` is the caller's responsibility.

### 3.3 Session ID is carried as a tag

`McpServer::publish_inbound_notification()` (`src/mcp/mcp_server.cpp:674`) writes `m.tags["session_id"] = current_session_->id` for inbound `notifications/*`. `current_session_` is per-request thread-local context, so the tag faithfully identifies the **publisher** session. `MessageOrigin` (`include/etil/manifold/message.hpp:50`) holds `(hostname, app_startup_us, session_id, seq)` but `current_origin()` returns process-wide identifiers — the tag, not the origin tuple, is the per-session marker on inbound traffic.

### 3.4 `mcp-on-progress` does not filter

`prim_mcp_on_progress` calls `mcp_on_channel_internal(ctx, "etil.mcp.in.progress")` (`src/manifold/til_primitives.cpp:1040`), which calls `svc->observe(pattern, ctx.permissions())`. The returned observable emits **every** message published on `etil.mcp.in.progress`, regardless of which session published. So two sessions sharing the channel see each other's traffic out of the box.

### 3.5 SSE-out isolation is provided by `active_sink`, not by routing

The risk in (3.2)–(3.4) is that session B's outbound notifications might leak to session A. They don't — but the isolation is at the HTTP transport layer, not Manifold:

- `mcp_sse_out_sink` is registered with `DeliveryMode::Inline` (`src/mcp/mcp_server.cpp:82`), so the sink runs on the publisher's request thread.
- The sink calls `transport_->send()` (`src/mcp/http_transport.cpp`), which writes to a `thread_local active_sink` bound to the current HTTP response.
- Worker / dispatcher threads have no `active_sink`; they take the buffered `pending_notifications_` path, which is drained on the next GET /mcp by the *same* session.

Cross-session SSE leakage is therefore prevented by thread-locality of `active_sink`, not by anything Manifold does. Any future change that runs the SSE-out sink async would need to add explicit session targeting at the sink.

### 3.6 RBAC: `etil.mcp.in.progress` is not hard-wired

Hard-wired bypass set: `etil.aaa.audit.**`, `etil.security.**`, `etil.system.bootstrap.**`, `etil.logging.error`, `etil.mcp.in.cancelled`. `etil.mcp.in.progress` is *not* in this set; it requires the `receive_progress` role flag (`inbound_receive_allowed()`, `src/manifold/rbac.cpp:990`). To wire two sessions for cross-session pub/sub today, both roles need that flag plus a `channel_grants` entry granting Read on the relevant pattern.

### 3.7 Echo / cycle detection is process-wide

`DefaultChannelService::publish` enforces three-layer cycle defense (visited-trace, hop-TTL, origin echo) — `src/manifold/default_service.cpp:540–600`. Origin echo compares `(hostname, app_startup_us)` against the local origin. Because every session lives in the same process, those identifiers are identical for all sessions; the echo check **cannot distinguish** a true loop (A → B → A on the same publisher) from a legitimate reply path (A → B → A's other subscriber). This becomes load-bearing if/when bidirectional ITC is built.

### 3.8 Inline vs async delivery (Phase 5a)

`DeliveryMode::Inline` runs the sink on the publisher's thread; the default is the `ThreadDispatcher` queue. SSE-out and audit routes opt into Inline. Any new ITC routes must consider:
- Async (default): sink runs on dispatcher thread → no `active_sink`, no caller `ExecutionContext` → `type cr` inside an xt has nowhere to write.
- Inline: sink runs on publisher's thread → `type cr` writes to publisher's session output, **not** the subscriber's. This is precisely backwards for cross-session display.

**Neither dispatch mode delivers an xt's `type cr` to a *different* session's TUI without an explicit cross-thread plumbing step.**

---

## 4. Concrete blocker for the TUI use case

The original test was `mcp-on-progress 1 obs-take ' print-progress obs-subscribe` from a single TUI. It hangs until `--mcp-timeout` because:

1. `obs-subscribe` is a **terminal** operator that drains the source until completion.
2. The owning `interpret` JSON-RPC call therefore blocks until a message arrives.
3. While that call is in flight, the TUI's input box is disabled (`_command_in_flight=True`), so the same TUI cannot publish a `/notify` to satisfy the take(1).
4. A second TUI *would* satisfy it — but the xt's output (`type cr`) writes to whichever thread/context the sink runs on (see 3.8). Today that is **not** the subscriber session's TUI; it would either be the publisher's thread (Inline) or a dispatcher worker (async).

So the TUI flow does not work end-to-end today, even with two sessions, without code changes.

---

## 5. Gap analysis — what is missing for a real ITC substrate

1. **No per-session targeted (unicast) delivery.** Subscribers can read `tags["session_id"]` after the fact, but there is no route-level or sink-level "deliver only if `tags["session_id"]==X`" predicate. The closest existing precedent is `mcp_sse_out_sink`'s `target_user_id` tag, which is application logic, not a generic primitive.
2. **No xt-runs-in-subscriber-context invariant.** Async dispatch loses the original `ExecutionContext`. A subscriber's `type cr` cannot reach the subscriber session's TUI without a deliberate "tee subscriber output back to subscriber's session" plumbing step — which does not exist.
3. **No back-pressure scoped per subscriber.** Phase 5a `ThreadDispatcher` policies (RingBuffered / DropOldest / Block) are per-route, not per-subscriber. A slow session-B subscriber silently loses messages with no signal to session A.
4. **Echo detection is too coarse for bidi.** `(hostname, app_startup_us)` is shared across sessions in one process; legitimate A↔B request/response traffic could be dropped as a false-positive loop. Needs session-aware origin if bidi is ever required.
5. **No subscriber-→publisher reply correlation.** Manifold is fan-out only; request-response must be built on top of two channels and an application-level correlation ID.
6. **No session-lifecycle hooks for route cleanup.** Routes registered against a destroyed session become orphans. A `Session::on_destroy` cleanup pass is needed before unicast routing is durable.
7. **No standard "see another session's TIL output" channel.** `etil.repl.stdout` is teed per-session by the publishing session's `ExecutionContext` (see `core/execution_output_tee.cpp`); there is no convention for cross-session subscription to it. Adding this would be small but is currently undefined.

---

## 6. Minimum patch set for session-A → session-B unicast

Smallest viable change, in order of dependency:

1. **`RouteSpec::target_session_id` (optional).** Add a string field; default empty = no filter. ~10 LOC in the header plus one check inside the per-route fan-out loop in `DefaultChannelService::dispatch` (`src/manifold/default_service.cpp:537`). After the existing `channel_matches` and `tag_filter_matches` succeed, also require `state->spec.target_session_id.empty() || msg.tags["session_id"] == state->spec.target_session_id`. ~50 LOC of code + ~50 LOC of unit tests.
2. **TIL surface.** Either:
   - **Option A** (additive, conservative): new word `channel-subscribe-from ( session-id pattern -- observable )` that wires the new field. ~80 LOC.
   - **Option B** (orthogonal): new generic `channel-subscribe-with-tags ( pattern tags-map -- observable )` that lets the caller specify any `tag_filter`. More general, ~120 LOC; supersedes the targeted variant entirely.
3. **Subscriber-context tee.** Provide a route option (or a sink wrapper) that captures the *subscriber's* `ExecutionContext::out_` at subscribe time and reuses it on each delivery, so an xt's `type cr` reaches the subscriber's session output. This is the only way `mcp-on-progress` becomes useful from a second TUI without a polling workaround. Requires care under async dispatch (the captured `out_` must remain valid until route teardown). ~150 LOC + integration tests.
4. **RBAC role flag** `channels_cross_session: bool` (default false). Only roles with this flag may pass a non-empty `target_session_id` or subscribe with a tag filter referencing another session's id. ~30 LOC across `rbac.cpp` and `auth_config.cpp`.
5. **Session lifecycle hook.** `Session::~Session()` (or a dedicated `on_destroy`) iterates routes targeting this session id and unregisters them. ~60 LOC.

**Total:** roughly 400 LOC of production code + tests, behind an RBAC gate, with no breaking changes to existing routes. Risk concentrates in (3) — capturing `ExecutionContext::out_` across thread boundaries needs a clear ownership story and likely a `shared_ptr` lifetime for the tee target.

Phase 5a's async dispatch is a prerequisite for (3) to be useful at scale; without it, Inline delivery on the publisher's thread keeps blocking the publisher.

---

## 7. Honest verdict — local Manifold vs. broker

Manifold today is genuinely useful as an **observability / fan-out bus** within a single process. Hard-wired audit and security channels work. Logging migration via the seven named loggers works. SSE-out works because thread-local `active_sink` does the partition.

Manifold today is **not** an ITC substrate. The four hard requirements for ITC are:
- targeted (unicast) delivery,
- subscriber-context output capture,
- per-subscriber back-pressure,
- bidi correlation.

It has none of them.

| Capability | Manifold today | Manifold + §6 patch | NATS / AMQP |
|---|---|---|---|
| In-process low-latency fan-out | yes | yes | yes (with overhead) |
| Per-session unicast | no | yes | yes (subject conventions) |
| Subscriber output to subscriber's TUI | no | yes | n/a (out of scope) |
| Per-subscriber back-pressure | no | no | yes |
| Cross-process / multi-host | no | no | yes |
| Persistence / replay | no | no | yes (optional) |
| RBAC integrated with auth | yes | yes | external |
| No external dep | yes | yes | no |

**Recommendation:**
- For pure observability of inbound MCP notifications between sessions on one server, the §6 patch (focused on items 1, 3, 4, 5) is a worthwhile investment — it keeps secrets in-process and avoids deploying NATS for what is fundamentally a localhost concern.
- For multi-server, durable, or guaranteed-ordered traffic, keep the existing NATS / AMQP broker integration. The two are complementary, not competitive.
- For the immediate TUI test case driving this survey, the **smallest unblocker** is items (3) + (5) of §6: subscriber-context tee + lifecycle cleanup. Items (1) and (4) (targeted delivery + RBAC flag) can wait until a use case materializes that needs unicast.

---

## 8. Open questions

- Is there a use case requiring **ordered** cross-session delivery in-process? (None known today; if yes, push toward broker.)
- Should the `etil.repl.stdout` channel be promoted to a documented cross-session subscription target, or kept as a private session implementation detail? (The latter is current behavior and probably correct.)
- Does origin echo need a session-aware variant, or is per-route hop-TTL sufficient for the bidi loop case?

---

## 9. Decision gate

Before any code is written, decide:

- (A) Do we want Manifold to grow into an ITC substrate? If yes, schedule §6 as a feature branch.
- (B) Or do we accept that intra-process pub/sub is observability-only, and that all ITC must use the broker (or a still-undefined separate mechanism)?

The TUI's `/notify` family will work end-to-end against the existing channels regardless: option (A) makes the test loop self-contained on one server; option (B) requires running NATS in the deploy stack and using `channel-tap-nats` / `channel-source-nats` to bridge.
