# Manifold E2E Validation Plan

**Date:** 2026-04-26
**Status:** Plan input
**Audience:** ETIL server / Manifold authors
**Gates:** `20260426B-Cross-Session-Services-Architecture.md` §7
**Related:** `20260418B-IO-Channel-Pipeline-Architecture.md`, `20260419A-Manifold-Phase-3-4-Implementation-Plan.md`, `20260420A-Manifold-Phase-5a-Implementation-Plan.md`

---

## 1. Purpose

The cross-session ITC design (doc `20260426B`) is gated on Manifold being **proven by TIL-level end-to-end tests against the deployed ETIL server and the deployed message broker.** This document specifies the test taxonomy, the file layout, the driver harness, and the definition of done for that gate.

Existing C++ `test_manifold_*` unit and integration tests show Manifold is internally consistent. They do not show the language surface, the dispatcher, the broker bridges, and the deployment topology compose correctly when driven from real MCP sessions. That is what these tests establish.

---

## 2. Existing assets and what's missing

`tests/til/manifold-e2e/` already contains:

- `manifold_loop.til` — five local-loopback test cases (direct write/read; loop with 0/1/2 transforms; transform-rejection drops). Concurrency-aware: documents "install reader before writer; `channel-flush` drains async dispatcher; `1 obs-take` followed by `obs-subscribe` consumes exactly one value." **Driven today by a GoogleTest harness** (`test_manifold_loop_til_file.cpp`) that constructs a `ChannelService` and binds it to the interpreter, because the standalone REPL does not. This harness path is **not** the live server.
- `nats_loop.til` — one-way publish smoke test against the deployed NATS broker. Installs a tap, publishes 10 messages, ends. Has **no subscriber side**, so it does not exercise the round-trip and cannot detect a broken `channel-source-nats`.

**Gap:** neither file is currently exercised against the deployed MCP server, and the round-trip-via-broker path is not tested at all.

---

## 3. Test taxonomy

Two categories of test, both executed against the deployed server.

### 3.1 Local channel round-trip (E2E #1)

Pure in-process Manifold. The deployed server hosts a real `ChannelService`; the test verifies the language surface composes correctly with the dispatcher (Phase 5a `ThreadDispatcher`). This validates the foundation work that landed in v2.11.x (Inline vs async delivery, `channel-flush` semantics, hot-stream "install before publish" invariant).

Coverage to lift / port from `tests/til/manifold-e2e/manifold_loop.til`:
- Direct write + read on the same channel.
- Loopback with no transforms.
- Loopback with a single transform.
- Loopback with two chained transforms (insertion order).
- Transform returns false → drop; subsequent message passes.

Plus (new):
- **Concurrency stress:** N publishers fan-in to one channel, single reader takes M, asserts ordering / count. Smoke for the dispatcher under contention.
- **Cycle detection:** publish onto a route that loops back to itself; verify `channel-cycle-stats` reports the cycle and the message is dropped, not infinitely amplified.

### 3.2 Broker round-trip (E2E #2)

Validates that messages survive a round-trip through the deployed MQ. Pattern:

```
local channel A  --tap-->  MQ subject X  --source-->  local channel B
                                                          |
                                                       reader on B
                       writer publishes on A, reader on B receives it
```

Concretely:
1. `channel-source-nats nats://<svc>:4222 etil.test.<run-id> json etil.test.local.in` installs the source.
2. `obs-create-channel "etil.test.local.in" obs-message-read` installs the reader.
3. `channel-tap-nats nats://<svc>:4222 json etil.test.local.out` installs the tap (NATS subject is derived from channel name; consult Phase 3 docs for the exact derivation rule and update the test if the rule changed).
4. `obs-create-channel "etil.test.local.out" "<payload>" obs-message-write` publishes.
5. `channel-flush` drains.
6. `1 obs-take ' capture obs-subscribe` consumes.
7. Assert captured payload matches.

Variants:
- Single-message round-trip (smoke).
- Multi-message ordering preservation.
- Codec coverage: `json`, `msgpack`, `cbor`, `raw` — confirm Phase 3 codec-resolver actually round-trips each on the broker.
- Header round-trip: confirm `Msg-Host`, `Msg-Startup`, `Msg-Seq`, `Msg-OriginType` survive the wire and are recoverable on the source side.

NATS is the default broker (compose-deployed). If AMQP is in scope, mirror the test set with `channel-tap-amqp` / `channel-source-amqp`.

### 3.3 Why these are not unit tests

Both categories live at `tests/e2e/manifold/`, not `tests/til/`. Reasons:

- The gtest path (`tests/til/manifold-e2e/`) binds a synthetic `ChannelService` and runs in the build's process; it does not exercise the real server's session machinery, RBAC, or the HTTP transport.
- The MQ path requires a real broker. Mocking it defeats the test.
- Failures here block ITC work; failures at the gtest harness level do not, because they could be harness artifacts.

The existing gtest-driven `manifold_loop.til` stays where it is and continues running in CI as a fast regression net. The E2E variants are the gate.

---

## 4. Layout

```
tests/e2e/manifold/
├── README.md                  — how to run; prerequisites; expected output
├── local_loopback.til         — E2E #1 TIL test source (lift from manifold_loop.til + extras)
├── broker_loopback_nats.til   — E2E #2 (NATS) TIL test source
├── broker_loopback_amqp.til   — E2E #2 (AMQP), if and when AMQP is in scope
├── harness/
│   ├── run_e2e.sh             — bash driver: spins up MCP session, runs a .til, scrapes results
│   └── run_e2e.py             — alternative Python driver (cleaner JSON-RPC, optional)
└── expected/
    ├── local_loopback.expected
    └── broker_loopback_nats.expected
```

`tests/e2e/` is a new top-level directory; nothing else lives under it yet. Top-level `CMakeLists.txt` does **not** wire these into CTest — they are out-of-band, manual, and explicitly require the deployed server. CI is unchanged.

---

## 5. Driver harness

Two viable drivers. Pick one for v1; the other can follow if useful.

**Bash via `etil-tui --exec`** (preferred — already documented in `nats_loop.til` comment):
- Initialize an MCP session, optionally login.
- Run `etil-tui --log --logdir /tmp --exec tests/e2e/manifold/<test>.til`.
- Capture session log; grep for `PASS:` / `FAIL:` markers emitted by the TIL test's existing `harness.til` `pass` / `fail` words (already used by `manifold_loop.til`).
- Exit 0 only if every test in the file emits PASS and no FAIL.

**Bash via `curl`** (fallback when TUI is unavailable or for headless CI runners):
- POST `initialize`, capture `Mcp-Session-Id`.
- POST `tools/call interpret` with the TIL source as the `code` argument.
- Parse response JSON for the captured stdout, grep PASS/FAIL markers.
- Implementations like the existing CLAUDE.md smoke-test snippet are a good starting point.

In either case the harness is a single self-contained script under `tests/e2e/manifold/harness/`. No GoogleTest, no CTest, no implicit invocation.

---

## 6. RBAC requirements

The role driving the tests must have:
- `channels_enabled: true`
- `channel_grants` for `etil.test.**` (Read + Write + Route, allow).
- `channels_network_sink: true` for E2E #2 (NATS/AMQP taps and sources require it).
- `channels_route_admin: true` if `obs-loop-channels` is exercised.

Recommend a dedicated `e2e_test` role in `roles.json.example`, scoped to `etil.test.**` patterns only. The harness logs in as a user mapped to that role.

---

## 7. Definition of done

The gate opens when:

1. `tests/e2e/manifold/local_loopback.til` exists, covers §3.1, and PASSes against the deployed server.
2. `tests/e2e/manifold/broker_loopback_nats.til` exists, covers §3.2, and PASSes against the deployed server with the deployed NATS broker.
3. The driver script in `harness/` runs both tests in sequence and exits 0.
4. A short follow-up doc lands as `docs/claude-design/<date>-Manifold-E2E-Validation-Results.md` with: harness invocation, observed publish→deliver latencies (coarse `sleep`/timestamp diffs are acceptable for v1), any failures-and-fixes, and explicit gate-open declaration.

Until item 4 lands, doc `20260426B` remains design input only — no Layer A or Layer B implementation begins.

---

## 8. Out of scope (deferred)

- Hard latency / throughput benchmarks (those are doc `20260426B` §8 step 3; they come after correctness is established).
- AMQP coverage (mirror of NATS coverage; ship if and when AMQP is enabled in the deployment).
- Cross-session E2E (that's what doc `20260426B` is about; this validation precedes that work).
- Service Registry tests (does not exist yet).

---

## 9. Risks

- **`channel-source-nats` may not be exercised end-to-end in any existing test.** This validation could surface real bugs in subscribe-side codec/header handling. That's the point — better to find them now than during ITC implementation.
- **Hot-stream timing:** the documented "install reader before writer" invariant requires the harness to issue the TIL forms in the right order within a single `interpret` call. If the deployed server's dispatcher introduces enough latency that even same-call ordering is racy, `channel-flush` semantics need re-examination before the gate can open.
- **Deployment dependency:** these tests cannot run in CI as configured today. They are a manual prerequisite gate, not an automatic check. Document who runs them and how often (recommend: before any ITC merge, on every Manifold-touching merge).
