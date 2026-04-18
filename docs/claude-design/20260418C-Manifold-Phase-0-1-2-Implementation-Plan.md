# Manifold — Phases 0, 1, 2 Implementation Plan

**Date:** 2026-04-18
**Version basis:** v2.3.11 (current master)
**Target feature branch:** `manifold-foundation`
**Designs implemented:** `20260418A-Logging-Infrastructure-Survey.md` (substrate)
and `20260418B-IO-Channel-Pipeline-Architecture.md` (Manifold library).
**Workflow:** Per `docs/claude-knowledge/20260403A-feature-branch-workflow.md`.
**Speed bumps to avoid:** Per `docs/claude-knowledge/20260404-speedbumps.md`.
**Status:** Plan (not yet started).

---

## Scope

Single feature-branch / super-push sprint covering **three phases** as
defined in doc B §12 and §19.12:

- **Phase 0** — logging substrate. Create `src/core/logging.{hpp,cpp}`,
  migrate the 41 raw `fprintf(stderr)` sites catalogued in doc A §2 to
  spdlog, enforce the no-direct-stdio policy from doc A §11. No
  Manifold dependency; independently valuable and a prerequisite for
  Phase 1.
- **Phase 1** — Manifold core (`libetil-manifold.a`, namespace
  `etil::manifold`). Delivers: §3 core types with message identity
  (§18) and cycle detection (§20) fields, §4 in-process sinks, §5 core
  transforms, §9 dependency-injection service, §15 RBAC as the
  seventh `RolePermissions` domain, §22.2 sink-overflow / drop-
  visibility semantics. Also §17 Phase A — rewire `sys-notification` /
  `user-notification` through channels without changing TIL signatures.
- **Phase 2** — Observable bridge (§6) + TIL control surface (§7 and
  §21) + §17 Phase B (implement `GET /mcp` SSE endpoint and new
  `mcp-on-*` inbound observation words, add the four new
  `RolePermissions` fields).

**Out of scope** for this sprint:
- Phase 3 (broker sinks — Artemis / NATS / Kafka / Pulsar).
- `channel-tap-udp` and network-sink RBAC enforcement (Phase 3 gate).
- §19 etil-web browser sinks / sources. Those land in the separate
  `etil-web` repo as a sibling sprint (Phase 1b = console + error
  sources to replace the TypeScript glue layer's ad-hoc console use).
  Note in the sprint retrospective that etil-web can start in parallel
  once Phase 1's C++ API is stable.
- EvolveLogger absorption into Manifold channels (§22 Q7 answer said
  integrate; §22.3 A-2 notes the mechanics are unspecified). Defer to
  a dedicated follow-up sprint after this one lands — it requires a
  full re-plan of the 105 EvolveLogger call sites and of the four
  `evolve-log-*` TIL words.

---

## Pre-flight

Per workflow doc §1:

1. Master branch, working tree clean (no staged, unstaged, or
   untracked changes). Confirm with `git -C evolutionary-til status`.
2. Create feature branch: `scripts/branch.sh manifold-foundation`.
   Automated pre-flight checks fire; on success minor version bumps
   v2.3.11 → v2.4.0 and commits the version change.

Version progression across the sprint:

| Event                       | Version | Tool                                |
| --------------------------- | ------- | ----------------------------------- |
| Branch creation             | v2.4.0  | `scripts/branch.sh manifold-foundation` |
| Phase 0 start               | v2.4.1  | `scripts/version-bump.sh patch`     |
| Phase 1 start               | v2.4.2  | `scripts/version-bump.sh patch`     |
| Phase 2 start               | v2.4.3  | `scripts/version-bump.sh patch`     |
| Integration validation      | v2.4.3  | (no bump — same-phase validation)   |
| Documentation updates       | v2.4.3  | (no bump)                           |
| Merge / tag / push          | v2.4.3  | `scripts/super-push.sh`             |

Per workflow rule: **bump at phase start**, not at phase end. The
version in `CMakeLists.txt` always identifies the work in progress.

---

## Ambiguities to resolve during the sprint

Tracked from doc B §22.3. All four pre-sprint ambiguities are now
**resolved** (2026-04-18). Resolutions land back into doc B §22.3
during the documentation-updates commit.

- **A-1 — Channel name version-segment format. Resolved: integer-only.**
  Drop semver. Version segment, when present, is a plain integer
  (`etil.foo.bar.1`, `etil.foo.bar.2`). Wildcards match version
  segments as normal segments. No dotted-semver support —
  `etil.foo.bar.2.0.0` is not a legal versioned channel name.
- **A-3 — spdlog-in-WASM foundation. Resolved: dual-backend façade.**
  `src/core/logging.{hpp,cpp}` presents a single public API; on
  native it is backed by spdlog, on WASM (`ETIL_WASM_TARGET=ON`) by
  a minimal direct console backend that forwards to
  `console.log/warn/error/debug` via `EM_ASM`. Callers see the same
  `logging::get(name)->info(...)` surface in both builds.
- **A-4 — `MessageOrigin.hostname` format divergence. Resolved: accept
  divergence; add `origin_type` discriminator.** Native fills
  `hostname` from `gethostname()`; browser fills it from
  `location.origin`. Every `MessageOrigin` also carries an
  `origin_type` enum (`Native` | `Browser`) so cross-deployment
  consumers can discriminate without string-parsing the hostname.
  The enum travels alongside the origin tuple in broker serialization
  (§16.5 doc B) and is exposed through the TIL introspection path.
- **A-6 — `channel-origin` return shape. Resolved: HeapMap with
  alphanumeric keys.** Three stack items is too much for a single
  introspection word. `channel-origin` returns a single `HeapMap`
  with the keys: `host` (string), `startup` (integer, microseconds
  since epoch), `session` (string; empty or `"system"` sentinel when
  no session is bound), `origintype` (string; `"native"` or
  `"browser"`). Callers pick fields with standard map access words.
  The separate `channel-seq ( -- seq )` and
  `channel-last-published ( -- msg-id-str )` words are unchanged —
  both return a single value.

**Deferred to post-sprint:** A-2 (EvolveLogger absorption mechanics),
A-5 (broker session-scoping default — not applicable until Phase 3).

---

## Phase 0 — Logging substrate (v2.4.1)

**Goal:** Eliminate every raw-stderr-as-logging site in `src/` and
establish the spdlog-backed factory that Phase 1's `spdlog_sink` and
all surviving bootstrap paths will consume.

**Ties to doc A sections §2, §8, §11.**

### Tasks

1. **Bump patch**: `scripts/version-bump.sh patch` → v2.4.1.
2. **Create `include/etil/core/logging.hpp` and `src/core/logging.cpp`**:
   - `namespace etil::core::logging`.
   - `init(const std::string& log_dir, spdlog::level::level_enum default_level)`.
   - `shutdown()`.
   - `get(const std::string& name)` returning a cached
     `std::shared_ptr<spdlog::logger>`.
   - Named loggers: `etil.mcp`, `etil.http`, `etil.db`, `etil.aaa`,
     `etil.oauth`, `etil.session`, `etil.dict`. Factory-instantiated
     lazily on first `get()`.
   - Rotating file sink (`spdlog::sinks::rotating_file_sink_mt`) +
     stderr sink mirror for levels ≥ WARN.
   - Pattern: `[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v`.
3. **CMake two-backend façade** (A-3 resolution): `ETIL_WASM_TARGET=ON`
   swaps the spdlog-backed implementation for a direct
   `console.log/warn/error` backend via Emscripten's `EM_ASM` with
   the same public API. Native `logging::get(name)` returns a
   spdlog logger; WASM returns a thin wrapper presenting the same
   `->info()` / `->warn()` / `->error()` / `->debug()` surface that
   forwards to the browser console.
4. **Runtime level control**:
   - Env var `ETIL_LOG_LEVEL` (values: `off|trace|debug|info|warn|error|critical`).
   - CLI flag `--log-level <level>` on `examples/mcp_server.cpp`.
   - TIL word `log-level! ( level-str -- )` and `log-dir! ( path-str -- )`
     for REPL control (low priority; can defer to Phase 2 if time-constrained).
5. **Migrate the 41 `fprintf(stderr)` sites** from doc A §2, one
   subsystem per commit group within this phase:
   - `src/db/mongo_client.cpp` (16 sites) → `etil.db` WARN/ERROR.
   - `src/mcp/mcp_server.cpp` (12 sites) → `etil.mcp` INFO/WARN/ERROR.
   - `src/mcp/http_transport.cpp` (4 sites) → `etil.http` INFO/ERROR.
   - `src/mcp/oauth_github.cpp` (2 sites) → `etil.oauth` WARN.
   - `src/mcp/oauth_google.cpp` (3 sites) → `etil.oauth` WARN.
   - `src/aaa/audit_log.cpp` (1 site) → `etil.aaa` ERROR.
   - `src/aaa/user_store.cpp` (3 sites) → `etil.aaa` ERROR.
6. **Existing spdlog uses** (`src/core/dictionary.cpp:35`,
   `src/mcp/mcp_server.cpp:270`): migrate from the global default
   logger to named loggers (`etil.dict`, `etil.session` respectively).
7. **Pre-commit grep check**: add a hook or script entry that
   rejects new diffs under `src/` / `include/` / `examples/` that
   introduce `fprintf\s*\(\s*stderr`, `std::cerr\s*<<`, `printf\s*\(`,
   or `std::cout\s*<<` outside the three documented bootstrap
   exceptions (doc A §11.2). Allowlist those exception sites by
   file:line.
8. **Unit tests** (`tests/unit/test_logging.cpp`):
   - Factory returns the same cached instance for repeated `get()`.
   - Level filter respects `ETIL_LOG_LEVEL` env var at init.
   - Rotating sink rotates at configured size.
   - Named loggers have independent levels.
   - WASM-backend test guarded by `ETIL_WASM_TARGET`.
9. **Build / test**: `scripts/build.sh all` and `scripts/test.sh all`
   (tee output to `/tmp/p0-build.log` and `/tmp/p0-test.log` per
   speedbumps knowledge).

### Completion criteria

- All 41 stderr sites now route through named spdlog loggers.
- No new raw-stderr additions possible without tripping the pre-commit
  check.
- `test_logging.cpp` passes on debug + release + WASM builds.
- `grep -rn 'fprintf\s*(\s*stderr' src/ include/` returns only the
  allowlisted bootstrap sites (examples/mcp_server.cpp argparse,
  examples/simple_repl.cpp config-load warnings).

### Commit

```
v2.4.1 Phase 0 — Logging substrate and stderr migration

- Add src/core/logging.{hpp,cpp} with named-logger factory, rotating
  file sink, env/CLI level control, and dual spdlog/console backend
  for native vs WASM builds.
- Migrate 41 fprintf(stderr) sites across 7 files (mongo_client,
  mcp_server, http_transport, oauth_github, oauth_google, audit_log,
  user_store) to named spdlog loggers per doc A §2.
- Migrate existing spdlog default-logger uses (dictionary, mcp_server
  session eviction) to named loggers.
- Add pre-commit grep check blocking new raw-stderr logging additions.
- Add tests/unit/test_logging.cpp covering factory, levels, rotation,
  named-logger isolation.

Implements doc A §8, §11. No Manifold dependency.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Phase 1 — Manifold core + RBAC + outbound SSE rewire (v2.4.2)

**Goal:** Deliver the Manifold library in-process — core types, sinks,
transforms, service, RBAC, cycle detection, drop visibility — and
rewire `sys-notification` / `user-notification` onto channels without
changing TIL-visible behavior.

**Ties to doc B sections §3, §4, §5, §6 (minimal portion), §8, §9,
§15, §17 Phase A, §18, §20, §22.2.**

### Tasks

1. **Bump patch**: `scripts/version-bump.sh patch` → v2.4.2.
2. **Resolve A-1** (channel naming) — pick integer-only version
   segment format. Update doc B §19 and any other references.
3. **Create `include/etil/manifold/` and `src/manifold/` trees**:
   - `message.hpp` — `MessageOrigin`, `Message` structs per doc B §3.
   - `origin.hpp` — origin-tuple construction, hostname capture,
     `app_startup_us`, atomic `seq_counter_`. Per §18.4 + A-4
     resolution (add `origin_type` tag on init).
   - `sink.hpp` — `ISink` abstract with `accept()` and `flush()`.
   - `transform.hpp` — `ITransform::apply()` → `vector<Message>`.
   - `service.hpp` — `ChannelService` interface with `publish`,
     `add_route`, `remove_route`, `observe` (body of `observe` stubbed
     until Phase 2).
   - `route_spec.hpp` — `RouteSpec` with `DeliveryMode` / `buffer_capacity`
     / `OverflowPolicy` fields per §22.2.
4. **Implement `DefaultChannelService`** (`src/manifold/default_service.cpp`):
   - Thread-safe route table (`absl::Mutex` + `absl::flat_hash_map`).
   - Publish path: origin stamp → cycle check (trace + TTL) → route
     match → per-route delivery (RingBuffered or Inline).
   - Origin echo suppression slot (layer 3); actual broker-source
     integration lands in Phase 3 but the hook exists now.
   - Per-route drop counters with periodic summary emission on
     `etil.manifold.sink.drops` (per §22.2).
   - First-drop and burst-threshold events on
     `etil.manifold.sink.drop-event`.
5. **Built-in sinks** (in `src/manifold/sinks/`):
   - `spdlog_sink` — bridges to `logging::get(name)` from Phase 0.
   - `file_sink` — direct append, rotation optional, used in Inline
     mode by hard-wired audit/security channels.
   - `stderr_sink` — bootstrap exception path only.
   - `observable_sink` — pushes onto a `HeapObservable` (full wiring
     in Phase 2; stub API here).
   - `ring_buffer_sink` — bounded in-memory tail.
   - `test_capture_sink` — deterministic vector-collecting sink for
     unit tests.
   - `null_sink` — discards.
6. **Built-in transforms** (in `src/manifold/transforms/`):
   - `level_filter`, `channel_filter`, `tag_filter`, `tag_annotator`,
     `formatter`, `rate_limiter`, `fan_out`, `sampler`, `json_encoder`.
7. **RBAC (§15)**:
   - Extend `include/etil/mcp/role_permissions.hpp` with the seventh
     domain (channels) per §15.2 — `channels_enabled`,
     `channel_grants`, `channel_publish_quota`, `channel_subscribe_quota`,
     `channels_route_admin`, `channels_network_sink`.
   - `ChannelGrant` struct with `pattern`, `actions` bitmask,
     `Effect`.
   - `ChannelAction` bitmask enum (Read / Write / Route / Introspect).
   - Static `kHardwiredChannels` table with delivery-mode column per
     §15.5 + §22.2. Hard-wired Write subtrees (`etil.aaa.audit.**`,
     `etil.security.**`, `etil.system.bootstrap.**`, `etil.logging.error`)
     register as `Inline` delivery automatically.
   - Decision procedure implemented in `ChannelService::publish` and
     `::observe` per §15.3.
   - Every denial emits on `etil.aaa.audit.channel.denied` (hard-wired
     write, Inline).
8. **Cycle detection (§20)**:
   - Layer 1: `route_trace` append-and-check in the publish path.
   - Layer 2: `hops_remaining` decrement + TTL-exhausted audit.
   - Layer 3: origin echo-suppression hook (broker integration
     deferred to Phase 3; hook is in place).
   - Hard-wired audit bypass so cycle / TTL audits can always emit.
9. **Static route-graph validation** (§20.6): Tarjan's SCC on
   registered routes at `add_route()` time; configured cycles emit
   warnings on `etil.logging.warn`.
10. **§17 Phase A — outbound SSE rewire**:
    - `ExecutionContext::notification_sender_` (`include/etil/core/execution_context.hpp:441`)
      becomes a closure that calls
      `channels->publish(Message{channel="etil.mcp.out.notification.system", ...})`.
    - Same for `targeted_notification_sender_` →
      `etil.mcp.out.notification.user` with `target_user_id` tag.
    - `mcp_sse_out_sink` attaches per session in
      `src/mcp/tool_handlers.cpp:513`; filters on `session_id` tag
      and writes the SSE stream.
    - The per-request `pending_notifications_` thread-local buffer in
      `http_transport.cpp:24` becomes the ring buffer of
      `mcp_sse_out_sink` (preserved semantics, new home).
    - **TIL words `sys-notification` and `user-notification` signatures
      unchanged.** Legacy permission bools (`send_system_notification`,
      `send_user_notification`) remain authoritative per §17.4.
11. **Unit tests** (`tests/unit/`):
    - `test_manifold_service.cpp` — publish/subscribe, route add/remove,
      thread safety, origin uniqueness, sequence monotonicity.
    - `test_manifold_cycle.cpp` — layer-1 trace detection, layer-2
      TTL exhaustion, layer-3 echo suppression hook, hard-wired
      audit bypass.
    - `test_manifold_rbac.cpp` — decision procedure matrix: default
      deny, master-off, grant allow, explicit deny, hard-wired
      bypass, quota exhaustion. Property test: random role + random
      channel operation → deterministic decision.
    - `test_manifold_sinks.cpp` — each built-in sink individually,
      both RingBuffered (drop_first default, drop_last opt-in) and
      Inline delivery.
    - `test_manifold_sse_out.cpp` — sys-notification round-trips
      through channels to the SSE writer; user-notification routes
      correctly by user_id tag; legacy permissions gate writes.
12. **Build / test**: `scripts/build.sh all` teed to `/tmp/p1-build.log`,
    `scripts/test.sh all` teed to `/tmp/p1-test.log`.

### Completion criteria

- All unit tests pass on debug + release.
- No regression in existing MCP SSE tests
  (`tests/docker/` MCP HTTP tests still pass).
- Manifold's `DefaultChannelService` dispatches correctly in
  multi-threaded stress test (100 producer threads × 10k messages
  each → no lost messages on inline routes, deterministic drop counts
  on ring-buffered routes).
- The MCP server executable, when run with debug build + ASan, completes
  an existing smoke test (initialize → interpret → notification
  round-trip) with no ASan errors.

### Commit

```
v2.4.2 Phase 1 — Manifold core, RBAC, cycle detection, outbound SSE

- Add libetil-manifold.a (namespace etil::manifold) with Message +
  MessageOrigin + Sink + Transform + Service interfaces and
  DefaultChannelService implementation.
- Message identity: (hostname, app_startup_us, session_id, seq) tuple
  with process-global atomic seq counter per doc B §18.
- Cycle detection: three layers (visited-trace, hop-TTL, origin echo)
  per §20. Hard-wired audit bypass.
- Sinks: spdlog, file, stderr, observable (stub), ring buffer, test
  capture, null. Transforms: level/channel/tag filters, annotator,
  formatter, rate limiter, fan-out, sampler, json encoder.
- Delivery modes per §22.2: RingBuffered (default 16, drop_first) or
  Inline (hard-wired audit/security bypass ring buffer). Drop
  visibility: counters + periodic summary on etil.manifold.sink.drops.
- RBAC: 7th RolePermissions domain (channels_enabled master switch,
  channel_grants, route_admin, network_sink) per §15. Hard-wired
  channel table with inline delivery for etil.aaa.audit.**,
  etil.security.**, etil.system.bootstrap.**, etil.logging.error.
- §17 Phase A: ExecutionContext notification callbacks rewired to
  publish onto etil.mcp.out.notification.{system,user} channels.
  sys-notification / user-notification TIL signatures unchanged.
- Ambiguities resolved: A-1 (integer-only version segments), A-4
  (accept hostname format divergence; add origin_type discriminator).
- Tests: 5 new test files covering service, cycle, RBAC, sinks, SSE.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Phase 2 — Observable bridge, TIL surface, inbound SSE (v2.4.3)

**Goal:** Make Manifold scriptable from TIL and close the inbound SSE
gap — `GET /mcp` SSE endpoint currently returns "not yet supported"
at `http_transport.cpp:189, 368`.

**Ties to doc B sections §6, §7, §17 Phase B, §21.**

### Tasks

1. **Bump patch**: `scripts/version-bump.sh patch` → v2.4.3.
2. **Observable bridge (§6)**:
   - `ChannelService::observe(pattern)` fully implemented, returning
     a `HeapObservable<Message>`.
   - Per-channel backing observable managed inside the service.
   - Sinks attached to an observable appear as standard observers.
3. **TIL word control surface** (29 new words — see doc B §21):
   - **Core (§21.1)**: `channel-publish`, `channel-subscribe`,
     `channel-route-add`, `channel-route-remove`, `channel-tap-file`,
     `channel-tap-observable` (NOT `channel-tap-udp` — Phase 3
     gated), `channel-list`, `channel-list-routes`.
   - **RBAC (§21.2)**: `role-grant-channel`, `role-revoke-channel`,
     `role-channel-enable!`, `role-network-sink!`, `channel-perm-check`,
     `channel-perm-list`.
   - **Identity (§21.4)**: `channel-origin ( -- map )` returning a
     `HeapMap` with alphanumeric keys `host` / `startup` / `session`
     / `origintype` per A-6 resolution. `channel-seq ( -- seq )` and
     `channel-last-published ( -- msg-id-str )` remain single-value
     returns.
   - **Cycle + drop visibility (§21.5)**: `channel-trace`,
     `channel-hops-left`, `channel-cycle-stats`, `channel-sink-stats`,
     `channel-all-sink-stats`.
4. **§17 Phase B — inbound SSE endpoint**:
   - Implement `GET /mcp` handler at `http_transport.cpp:189, 368`
     (delete the "not yet supported" response and add real SSE
     streaming with session binding and keep-alive heartbeats).
   - `POST /mcp` already exists for client notifications; tag the
     parsed JSON-RPC message with the session and publish onto
     `etil.mcp.in.notification.*` / `etil.mcp.in.progress` /
     `etil.mcp.in.cancelled` / `etil.mcp.in.roots.changed` /
     `etil.mcp.in.request.*` based on the `method` field.
   - **Threading**: libuv I/O thread publishes onto the channel; the
     session's interpreter drains a per-session ready queue at
     cooperative yield points. Reuse the pattern from
     `src/core/observable_execution.cpp`.
5. **`mcp-on-*` TIL words (§21.3)**:
   - `mcp-on-notification`, `mcp-on-progress`, `mcp-on-cancelled`,
     `mcp-on-roots-changed`, `mcp-on-request`. Each returns a
     `HeapObservable` subscribed to the corresponding inbound channel.
   - Session-tag filter applied automatically so a subscription only
     sees its own session's messages.
6. **New `RolePermissions` fields for inbound SSE** (§17.4):
   - `receive_client_notification`, `receive_progress`,
     `receive_cancelled` (default `true`), `receive_roots_changed`,
     `mcp_subscribe_quota`.
   - Hard-wired: `etil.mcp.in.cancelled` gets Read-hard-wired for
     the owning session (cancellation must be honored regardless
     of role config). Add to `kHardwiredChannels`.
7. **Help text**: `data/help.til` entries for every new TIL word.
8. **Unit and integration tests**:
   - `test_manifold_observable.cpp` — `observe()` returns observable
     that fires on publish; subscription lifecycle ties to
     ExecutionContext; unsubscribe removes the route.
   - `test_manifold_til.cpp` — REPL-driven coverage of all 29 new
     words; permission-check failures return correct errors.
   - `test_manifold_sse_in.cpp` — simulate a client notification POST,
     verify it arrives on the correct `etil.mcp.in.*` channel with
     session tagging, verify `mcp-on-*` TIL subscription receives it,
     verify `etil.mcp.in.cancelled` read-hard-wired bypass works
     for a role that otherwise has no subscribe permission.
   - `test_manifold_drop_visibility.cpp` — flood a ring-buffered
     route, verify drop counter increments, verify
     `etil.manifold.sink.drops` receives the periodic summary,
     verify `etil.manifold.sink.drop-event` fires once on first
     drop and again on burst threshold.
9. **Build / test**: `scripts/build.sh all`, `scripts/test.sh all`,
   both teed to `/tmp/p2-*.log`.

### Completion criteria

- All 29 Phase-2 TIL words pass their REPL-driven tests.
- `GET /mcp` SSE endpoint accepts a connection and streams
  notifications; validated with the MCP smoke-test from workspace
  CLAUDE.md (adapted to exercise the new endpoint).
- `mcp-on-cancelled` fires for a session receiving a
  `notifications/cancelled` from the client even when the session's
  role has `receive_client_notification = false` (hard-wired bypass
  working).
- `test_manifold_drop_visibility.cpp` confirms operators can detect
  drops without polling.

### Commit

```
v2.4.3 Phase 2 — Observable bridge, TIL surface, inbound SSE

- ChannelService::observe returns HeapObservable; completes the
  Manifold → TIL bridge described in doc B §6.
- 29 new TIL words across core channel ops, RBAC admin and
  introspection, identity, cycle detection, drop visibility
  (see doc B §21). Help entries in data/help.til.
- §17 Phase B: implement GET /mcp SSE endpoint replacing the "not
  yet supported" stub at http_transport.cpp:189, 368. Inbound client
  notifications now publish onto etil.mcp.in.** channels.
- 5 new RolePermissions fields for inbound-SSE RBAC; etil.mcp.in.cancelled
  added to hard-wired Read channels (cancellation always honored).
- mcp-on-notification / progress / cancelled / roots-changed / request
  TIL words return HeapObservables subscribed to inbound channels
  with automatic session-tag filtering.
- Ambiguity resolved: A-6 channel-origin returns a HeapMap with
  alphanumeric keys host / startup / session / origintype.
  channel-seq and channel-last-published remain single-value returns.
- Tests: observable integration, TIL REPL coverage, inbound SSE
  round-trip, drop-visibility end-to-end.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Integration validation (v2.4.3, no version bump)

Separate commit on the feature branch.

### Tests

1. **End-to-end MCP round-trip under load.** 1000 concurrent client
   sessions, each issuing an interpret + receiving 10 notifications,
   over a 5-minute window. Validate:
   - No ASan errors (debug build).
   - No lost audit records (cross-check `etil.aaa.audit.**` file
     sink against per-session expected counts).
   - Drop counters on notification routes match expectation
     (near-zero unless deliberately overflowed).
2. **Cycle detection stress.** Configure a deliberate two-hop cycle
   through `channel-route-add` and publish 1000 messages. Verify:
   - Static warning fires at route-add.
   - Runtime cycle audit fires on first offending publish.
   - No message escapes the detection.
3. **RBAC matrix.** Script that constructs 8 synthetic roles with
   varying `channels_enabled` / `channel_grants` and exercises
   every hard-wired and non-hard-wired channel with all four
   actions. Verify decisions match the expected table.
4. **Logging policy enforcement.** Run the pre-commit grep check
   against the entire diff of the sprint — must pass with the
   documented allowlist.

### Commit

```
Integration validation for Manifold Phases 0-2

- End-to-end MCP round-trip load test (1000 sessions × 10
  notifications over 5 min, debug + ASan) passes with no lost
  audits.
- Cycle-detection stress test confirms both static and runtime
  layers catch a deliberate two-hop cycle.
- RBAC matrix exercises all four actions × 8 synthetic roles ×
  hard-wired and non-hard-wired channels, all match expected
  decisions.
- Pre-commit grep check validated against the sprint diff.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Documentation updates (v2.4.3, no version bump)

Per workflow doc §4, after integration validation passes.

### Files

1. **`README.md`** — Add "Manifold I/O channel pipeline" section.
   List the 35 new TIL words (aggregated from §21), name the seven
   named loggers, describe the no-direct-stdio policy.
2. **`data/help.til`** — Help entries for every new TIL word (29
   from Phase 2 + any from Phase 0 like `log-level!` / `log-dir!`).
   Help text must include the stack effect and the channel it acts
   on.
3. **`CLAUDE.md`** (project root) — Add "Manifold" to the
   architecture section. Note the library moniker, namespace, and
   reference the three design docs. Add a bullet to the coding
   standards: "No direct writes to stdout/stderr — use spdlog via
   `logging::get(name)` or publish onto a Manifold channel."
4. **`docs/claude-design/20260418B-IO-Channel-Pipeline-Architecture.md`**
   — Update §22.3 to mark A-1, A-3, A-4, A-6 as resolved with the
   decisions this sprint landed on. A-2 and A-5 remain deferred.

### Commit

```
Manifold docs — README, help.til, CLAUDE.md, design-doc updates

- README.md: Manifold section with 35 new TIL words and named-logger
  list.
- data/help.til: help entries for all new TIL words from Phases 0-2.
- CLAUDE.md: Manifold added to architecture; no-direct-stdio
  coding standard.
- 20260418B §22.3: mark A-1 / A-3 / A-4 / A-6 resolved with the
  decisions landed in this sprint. A-2 and A-5 remain deferred.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Merge and push

Per workflow doc §5-§6.

1. **Ask for permission** before `super-push.sh`.
2. `ETIL_GIT_REMOTE=<ci-remote> scripts/super-push.sh --message "Manifold foundation: Phases 0, 1, 2"`.
3. Wait for CI completion on the CI remote; verify the build succeeds.
4. **Ask for permission** before `github-push.sh`.
5. `scripts/github-push.sh` pushes master + tag to GitHub
   (ETIL-ORG/etil).

Super-push automates: build all, test all, commit any remaining
branch changes, switch to master, pull, merge `--no-ff`, resolve
any version conflicts, tag the final version, push to the CI
remote.

---

## Speed bumps to observe

Per `docs/claude-knowledge/20260404-speedbumps.md`:

- **No compound bash commands.** Never `cd X && cmd`; use
  `git -C <path>` / `scripts/build.sh` / absolute paths.
- **No command injection.** Never `ctest -j$(nproc)`; use
  `scripts/test.sh` or pre-compute `nproc` and substitute the
  literal value.
- **Tee long processes to `/tmp`.** Every `build.sh` / `test.sh`
  invocation in this sprint writes to `/tmp/pN-*.log` so the output
  is recoverable without re-running on a grep miss.
- **TIL comments use `#`.** Not `(`. Any `.til` test file added in
  Phases 1 or 2 uses `#` exclusively.
- **Control flow only inside colon definitions.** Any new test TIL
  files keep `if` / `while` / `begin` inside `:` ... `;`.

---

## Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- |
| Phase 1 too large for a single coherent commit | High | Medium | Intra-phase commits allowed per workflow §2 (bump only at phase start; fixes and incremental pieces share the version). Structure commit order: types → service → sinks → transforms → RBAC → cycle → SSE rewire → tests. |
| MCP SSE regression during §17 Phase A rewire | Medium | High | Run existing MCP Docker integration tests (`tests/docker/`) after each incremental commit in Phase 1; no regression gates phase completion. |
| 41-site migration introduces typos | Medium | Medium | Script the substitution where possible (`sed`-driven per-subsystem with review), and rely on build failures for the cases where message format arguments change. |
| Thread-safety bugs in ChannelService under load | Medium | High | Stress test under debug+ASan in integration validation; `absl::Mutex` with `ABSL_GUARDED_BY` annotations per project coding standards. |
| Hard-wired inline-delivery path stalls on slow sink | Low | High | §22.2 specifies Inline sinks must be fast (file append). Tests include a hard-wired audit write under slow-disk simulation; fall-back to InlineBounded if needed. |
| EvolveLogger absorption attempted mid-sprint | Low | High | Explicitly deferred in Scope; attempting it mid-sprint bloats Phase 1 beyond one commit. Do not touch the 105 EvolveLogger sites in this sprint. |
| Browser (etil-web) work mixed in | Low | Medium | Explicitly out of scope; etil-web sibling sprint waits until Phase 1 C++ API stable. |

---

## Dependencies

**Phase 0 depends on:** nothing. Can start immediately after branch
creation.

**Phase 1 depends on:** Phase 0 (uses `logging::get` for the
`spdlog_sink`). RolePermissions struct already exists in the repo
(`include/etil/mcp/role_permissions.hpp`); Phase 1 only adds fields.

**Phase 2 depends on:** Phase 1 core service, Phase 1 RBAC.

**No external repo changes.** etil-web is parallel but independent.
etil-tui does not consume Manifold directly in this sprint (it
observes via MCP notifications, which already work through §17 Phase
A's rewiring).

---

## Completion checklist

```
[ ] Feature branch created — scripts/branch.sh manifold-foundation → v2.4.0
[ ] Phase 0 — v2.4.1 bumped, logging module + 41 stderr sites migrated,
    tests added, committed
[ ] Phase 1 — v2.4.2 bumped, Manifold core + RBAC + cycle + SSE rewire,
    tests added, committed
[ ] Phase 2 — v2.4.3 bumped, observable bridge + 29 TIL words + inbound
    SSE + new RBAC fields, tests added, committed
[ ] Integration validation — end-to-end load, cycle stress, RBAC matrix,
    logging policy grep, committed
[ ] Documentation — README.md + help.til + CLAUDE.md + doc B §22.3
    ambiguity resolutions, committed
[ ] Permission granted — super-push.sh
[ ] super-push.sh — built, tested, merged, tagged v2.4.3, pushed to CI remote
[ ] Feature branch deleted (automatic via super-push.sh)
[ ] CI — build passed on CI remote
[ ] Permission granted — github-push.sh
[ ] github-push.sh — master + v2.4.3 tag pushed to GitHub
```

---

## Post-sprint

With Phases 0-2 landed, the follow-up sprints in priority order:

1. **etil-web Phase 1b** (separate repo) — wire the new `logging::`
   façade into the TypeScript glue layer and replace its ad-hoc
   console use with `console_source` bridging onto `etil.web.console.**`.
   Builds on A-3's dual-backend resolution.
2. **Manifold Phase 3** — broker sinks (AMQP via Qpid Proton-C++ /
   NATS via nats.c), network-sink RBAC gates, `channel-tap-udp`.
3. **EvolveLogger absorption** — the deferred work from A-2. Map 17
   category bits to channel names, migrate the 105 call sites,
   retire the four `evolve-log-*` TIL words (or rewire them as
   convenience wrappers).
4. **Manifold Phase 4** — remaining source migration (evolution
   fitness / selection / MCP request lifecycle) onto named channels.
5. **Optional: A-5 resolution** — broker-backed subscriber session
   scoping default (secure-by-default vs monitor-by-default).
