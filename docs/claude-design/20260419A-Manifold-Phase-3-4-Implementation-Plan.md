# Manifold — Phases 3 and 4 Implementation Plan

**Date:** 2026-04-19
**Version basis:** v2.4.3 (current master, Manifold Phases 0-1-2 shipped 2026-04-18)
**Target feature branch:** `manifold-broker-and-sources`
**Designs implemented:** `20260418B-IO-Channel-Pipeline-Architecture.md` §12, §16, §17
Phase C, §18.5, §20.3, §22.
**Workflow:** Per `docs/claude-knowledge/20260403A-feature-branch-workflow.md`.
**Speed bumps to avoid:** Per `docs/claude-knowledge/20260404-speedbumps.md`.
**Status:** Plan (not yet started).

---

## Scope

Single feature-branch / super-push sprint covering **two phases** of the
Manifold roadmap (doc B §12):

- **Phase 3 — Network sinks.** Broker-backed terminal sinks (`nats_sink`,
  `amqp_sink`) per doc B §16, the optional `mcp_notification_sink` family
  per doc B §17 Phase C, and **cycle-detection layer 3** (origin echo
  suppression) per doc B §20.3 — the layer deferred from Phase 1-2
  because it only becomes observable once broker round-trips exist.
  Broker-side **sources** land here too so the echo-suppression layer
  can be exercised end-to-end in a single integration test.
- **Phase 4 — Source migration.** Wrap the four legacy producers that
  still bypass Manifold as **channel sources** per doc B §12 Phase 4:
  `ExecutionContext::out_`, MCP request-handler lifecycle events, the
  evolution engine's progress/fitness events, and **EvolveLogger** (the
  105-call-site absorption deferred from the Phase 0-1-2 sprint as
  ambiguity A-2 per doc B §22.3). Old direct transports stay on as
  sinks on the respective channels so TIL behavior does not regress.

**Out of scope** for this sprint:
- Phase 5 — Android-Intent upper layer (doc B §11). Deferred until a
  concrete cross-subsystem event dispatcher need materializes.
- `kafka_sink` / `pulsar_sink` (doc B §16.5, §16.8). Those land "if/when
  concrete use cases emerge" — not this sprint.
- §19 etil-web browser sinks / sources. The separate `etil-web` repo
  owns those as a sibling `Phase 3b` rollout; they do not block this
  sprint's C++ surface.
- **A-5 — broker session-scoping default.** Resolves *during* Phase 3c
  below (decision entry, see §Ambiguities). Not deferred further.
- **Dynamic role-admin persistence to AuthConfig / MongoDB.** Phase 2d's
  `role-grant-channel` / `-revoke-channel` / `-channel-enable!` /
  `-network-sink!` words mutate session-scoped permissions only. Making
  those persistent is its own concern — defer to a role-admin sprint
  after this one.

---

## Pre-flight

Per workflow doc §1:

1. Master branch, working tree clean (no staged, unstaged, or untracked
   changes). Confirm with `git -C evolutionary-til status`.
2. CI pipeline green for v2.4.3. Spot-check the last pipeline log at
   `/opt/etil-ci/logs/20260419T025532-pipeline-all.log` to be sure we
   are branching from a known-good master.
3. Create feature branch: `scripts/branch.sh manifold-broker-and-sources`.
   Automated pre-flight checks fire; on success minor version bumps
   v2.4.3 → v2.5.0 and commits the version change.

Version progression across the sprint:

| Event                       | Version | Tool                                          |
| --------------------------- | ------- | --------------------------------------------- |
| Branch creation             | v2.5.0  | `scripts/branch.sh manifold-broker-and-sources` |
| Phase 3 start               | v2.5.1  | `scripts/version-bump.sh patch`               |
| Phase 4 start               | v2.5.2  | `scripts/version-bump.sh patch`               |
| Integration validation      | v2.5.2  | (no bump — same-phase validation)             |
| Documentation updates       | v2.5.2  | (no bump)                                     |
| Merge / tag / push          | v2.5.2  | `scripts/super-push.sh`                       |

Per workflow rule: **bump at phase start**, not at phase end. The
version in `CMakeLists.txt` always identifies the work in progress. Two
patch bumps match the two-phase shape; internal sub-tasks (3a/3b/3c/3d
and 4a/4b/4c/4d) commit separately under the phase's patch version, as
Phase 2a-d did in the prior sprint.

---

## Ambiguities to resolve during the sprint

Tracked from doc B §22.3 and doc B §13. All three must be decided
before the sub-task they gate lands. Resolutions fold back into doc B
§22.3 during the documentation-updates commit.

- **A-5 — broker session-scoping default.** Pre-Phase-3c decision.
  Question: when an `amqp_sink` / `nats_sink` publishes onto a broker
  topic, does `session_id` travel as a subject-path segment (and hence
  become a per-session *topic*), as a broker header, or as a payload
  tag only? **Proposed resolution:** carry an **HMAC of the session_id**
  — specifically `HMAC-SHA256(process_key, session_id)` truncated to
  128 bits and base64url-encoded — as a broker **header**
  (AMQP 1.0 application-property `session_hmac`; NATS header
  `Session-Hmac`). The same hashed token is mirrored in the serialized
  payload for brokers that drop headers on replay. **Raw session_id
  never crosses the broker boundary.** Subject paths stay
  session-agnostic so `etil.mcp.out.notification.*` is one topic, not
  one topic per session. Subscribers that already know the plaintext
  session_id (e.g., the TUI bound to its own session) HMAC it
  client-side using a published `channel-session-hmac ( str -- str )`
  TIL word and filter by the resulting token. External tools see an
  opaque-but-stable correlation key.

  **Security rationale.** The MCP `session_id` functions as a bearer
  credential — combined with the initial JWT it is the sole
  authenticator on every subsequent POST/GET via `Mcp-Session-Id`.
  Broadcasting it verbatim to every broker subscriber (operators,
  backups, journals, replay archives, third-party collectors) would
  let anyone with broker read access replay a still-live session
  against the MCP server. HMAC with a process-local key closes that
  path while preserving per-session filtering.

  **Process key lifetime.** The key is a 256-bit value generated at
  `ChannelService::init()` via a CSPRNG and held only in process
  memory alongside `app_startup_us`. It never reaches disk or the
  wire. Its scope matches `(hostname, app_startup_us)` — the same
  scoping already present in `MessageOrigin`. Consumers cannot
  correlate sessions across process generations without a side
  channel; that tradeoff is deliberate (matches the origin tuple's
  generation-boundary semantics) and acceptable for live tooling. A
  future key-escrow sink can address long-term archive correlation
  if that need materializes.

  **Non-goals.** The HMAC is not end-to-end authentication of the
  broker traffic — that is TLS + SASL / mTLS per doc B §16 and is
  unaffected by this change. The HMAC only prevents broker-side
  observers from reconstructing a usable session_id.

  **Topic-explosion rationale retained.** Subject-per-session would
  have produced thousands of short-lived topics on long-running
  deployments and fought NATS JetStream's consumer model. The
  session-agnostic subject + header-filter approach remains correct;
  the only change from the original proposal is hashing the header
  value.
- **A-2 — EvolveLogger absorption mechanics.** Pre-Phase-4d decision.
  Question: how do the existing `evolve-log-*` TIL words and the 105
  `EvolveLogger::log*()` call sites become Manifold publishers without
  breaking the existing log-file rotation / masked categories / evolution
  dashboards? **Proposed resolution:** keep `EvolveLogger` class as a
  thin adapter that forwards to `etil.evolution.**` channels; the
  existing log-file destination becomes a `file_sink` route on that
  channel prefix. Mask-bit categories (the `EvolveLogCat` enum) map
  1:1 to subchannel suffixes (`etil.evolution.fitness`,
  `etil.evolution.mutation`, etc.). `enabled(cat)` check becomes a
  `ChannelService::has_routes(channel)` lookup, preserving zero-cost
  semantics when a category has no consumers. TIL words unchanged in
  signature; call-site change is mechanical (search-and-replace of
  `EvolveLogger::inst().log*()` → `channels->publish(Message{...})`).
- **Broker-payload codec default.** Phase-3a gate. Question: does the
  broker sink default to `json_encoder` output (human-readable, large)
  or a compact binary codec (fast, opaque)? **Proposed resolution:**
  `json` default (existing `json_encoder` transform, UTF-8 string
  payload on the wire). Three additional codecs are opt-in per route:
  `msgpack` (MessagePack binary), `cbor` (CBOR binary), `raw`
  (pass-through, assumes payload already a `HeapByteArray`). Binary
  codecs produce a `HeapByteArray` payload — on the wire the broker
  sees an opaque byte sequence, not a UTF-8 string.

  **Control surface.** Both broker tap words take the codec as an
  explicit stack parameter:

  ```
  channel-tap-nats  ( url codec pattern -- handle )
  channel-tap-amqp  ( url codec pattern -- handle )
  ```

  where `codec` is one of `"json"` (default shorthand, also selected
  by empty string), `"msgpack"`, `"cbor"`, `"raw"`. The sink resolves
  the string at install time to a codec-transform and prepends it to
  the `RouteSpec` transform chain. Unknown codec strings fail the
  `channel-tap-*` call with TOS = `false` per the conditional-return
  convention (`CLAUDE.md` §Conditional Return Convention).

  **Rationale.** Broker consumers are human-debugging tools first
  (TUI, archiver, metrics collector); binary formats become worth the
  trouble only when payload volume dominates. The parameter form
  keeps codec selection visible at the route-install call site —
  clearer than a separate modifier word and avoids the ambiguity of
  "what does a codec switch mid-life mean" (it cannot happen).

  **Out of scope.** Protobuf, Avro, and schema-registry-backed codecs
  are deferred — they require a schema distribution story that
  neither sprint addresses. MessagePack and CBOR are schema-optional.

**Deferred to post-sprint** (explicitly not blocking):
- Kafka / Pulsar broker targets (doc B §16.5 Phase N).
- Role-admin dynamic grant persistence (own sprint).

---

## Phase 3 — Network sinks, broker sources, cycle layer 3 (v2.5.1)

**Design anchor:** doc B §16 (broker sinks), §17 Phase C
(`mcp_notification_sink`), §20.3 (origin echo suppression),
§18.5 (broker wire format).

Commit at the end of each lettered sub-task. All four sub-tasks land
at patch version v2.5.1 — the patch identifies the phase, the commit
message identifies the sub-task.

### Phase 3a — Broker-sink abstraction scaffolding (v2.5.1, commit 3a)

**Tasks:**
1. Add `include/etil/manifold/broker_sink.hpp` — shared `BrokerSink`
   abstract interface (inherits `ISink`) with connection config,
   topic-mapping policy, credential reference, codec slot. Matches the
   integration-shape bullets in doc B §16.5.
2. Add CMake options (matches doc B §16.7 Mode C):
   - `ETIL_BUILD_NATS_SINK` (OFF by default, ON in Docker + CI)
   - `ETIL_BUILD_AMQP_SINK` (OFF by default, ON in Docker + CI)
3. Add FetchContent superbuild entries in `ci/deps/`:
   - `nats.c` (Apache 2.0, pinned to latest 2025 release)
   - Qpid Proton-C++ (Apache 2.0, pinned to released tag)
4. Scaffold `src/manifold/broker_sink_base.cpp` with the codec pipeline,
   allowlist-check entry point, audit emission on connection failure
   (publishes onto `etil.logging.error` per doc B §16.5 bullet 5).
5. **Codec transforms** (per the broker-payload-codec resolution above):
   add three new `ITransform` implementations alongside the existing
   `json_encoder` from Phase 1:
   - `msgpack_encoder` — serializes the payload via a vendored
     MessagePack-C++ header (FetchContent pinned). Output payload type
     `HeapByteArray`.
   - `cbor_encoder` — same shape, CBOR output. Uses `nlohmann::json`'s
     CBOR helpers (already in the dependency graph) so no new dep.
   - `raw_passthrough` — asserts the incoming payload type is already
     `HeapByteArray`; forwards unchanged. Emits `etil.logging.error`
     on type mismatch.

   Codec-resolver helper `resolve_codec(std::string_view)` returns the
   matching transform or `nullptr` on unknown; lives in
   `src/manifold/codec_resolver.cpp`. The broker tap-word plumbing in
   Phase 3b/3c calls this helper exactly once at route install.
6. **Process-key generation for session HMAC** (per A-5): extend
   `ChannelService::init()` to generate a 256-bit key from a CSPRNG
   (`std::random_device` + `absl::BitGen` reseed, or
   `EVP_RAND_bytes` where OpenSSL is already linked). Store in a
   private member, zero on destruction. Expose
   `ChannelService::session_hmac(std::string_view session_id) const`
   → 128-bit truncated HMAC-SHA256 returned as a base64url string.
   No getter for the raw key. Add `include/etil/manifold/session_hmac.hpp`
   for the helper.
7. Unit tests: `test_manifold_broker_sink_base.cpp` — codec swap, config
   validation, allowlist rejection, error-channel emission on
   simulated connect failure. Runs without a live broker.
8. Unit tests: `test_manifold_codecs.cpp` — each codec round-trips a
   representative payload (string, int, HeapMap, nested); `resolve_codec`
   returns non-null for the four known names and null for unknown;
   `raw_passthrough` rejects a non-`HeapByteArray` payload with an
   `etil.logging.error` audit.
9. Unit tests: `test_manifold_session_hmac.cpp` — determinism
   (same session_id → same HMAC within one process), per-generation
   divergence (same session_id across two `init()` calls → different
   HMACs), output shape (22-char base64url), key is not leaked via
   any public accessor.

**Completion criteria:**
- `scripts/build.sh all` green with and without the broker flags.
- `scripts/test.sh all` green.
- `scripts/check-logging-policy.sh` passes (no new direct-stdio sites).
- New code is under `ETIL_BUILD_{NATS,AMQP}_SINK` compile gates so the
  default WSL build (broker flags OFF) stays lean.

### Phase 3b — nats_sink + NATS integration test (v2.5.1, commit 3b)

**Tasks:**
1. Implement `src/manifold/nats_sink.cpp` — subclass of `BrokerSink`
   using `nats.c`. Maps dotted channel names 1:1 to NATS subjects (doc
   B §16.4 — "our channel grammar maps 1:1"). Headers carry
   **`Session-Hmac`** (HMAC-SHA256 of session_id under the process
   key, truncated to 128 bits, base64url) and the full `MessageOrigin`
   per Ambiguity A-5. **Raw session_id never crosses the broker
   boundary.**
2. TIL word `channel-tap-nats ( url codec pattern -- handle )` registered
   via `register_manifold_primitives()`. `codec` is a string selecting
   one of `"json"` / `"msgpack"` / `"cbor"` / `"raw"` (empty string
   also selects `json`). Unknown codec → TOS = `false` per the
   conditional-return convention. Gated by `channels_network_sink`
   and the broker allowlist per doc B §16.6.
3. TIL word `channel-session-hmac ( str -- str )` — computes the same
   HMAC used in outbound headers so local subscribers can produce
   filter tokens from a known plaintext session_id. Safe for any role
   (no new RBAC gate); the key stays in-process and is never exposed
   via any introspection word.
4. Unit tests: `test_manifold_nats_sink.cpp` using the nats.c in-process
   test server embedded by `nats.c`'s test harness (no external
   infrastructure — gated by `ETIL_BUILD_NATS_SINK`). Test cases:
   connect OK, publish round-trip, header presence + shape,
   **raw session_id absence in wire bytes** (grep the serialized
   frame for the known plaintext — must not appear), allowlist reject,
   credential-scoping.
5. Docker E2E: add `tests/docker/test_manifold_nats.sh` that spins up
   a standalone `nats-server` container alongside the etil container
   and exercises publish/subscribe from both ends. Matches the existing
   MCP HTTP E2E test shape.

**Completion criteria:**
- All new tests green (`scripts/test.sh all` plus Docker E2E).
- `nats.c` superbuild builds clean under `ccache` on the CI host.
- Memory footprint check: nats-server idle + etil container remain
  under the 961 MB CI-host RAM budget. Record `docker stats` snapshot in
  the commit message.

### Phase 3c — amqp_sink + Artemis integration test (v2.5.1, commit 3c)

**Tasks:**
1. Implement `src/manifold/amqp_sink.cpp` — subclass of `BrokerSink`
   using Qpid Proton-C++. Maps dotted channel names to AMQP addresses
   1:1 (doc B §16.3: "Addresses are natively dotted with `*` and `#`"),
   translating `**` at publish time to `#` for broker matching.
2. TIL word `channel-tap-amqp ( url codec pattern -- handle )` parallel
   to `channel-tap-nats`. Same codec parameter semantics, same RBAC
   gates. Unknown codec → TOS = `false`.
3. Unit tests: `test_manifold_amqp_sink.cpp` using Proton's embedded
   mock container. Gated by `ETIL_BUILD_AMQP_SINK`.
4. Docker E2E: `tests/docker/test_manifold_artemis.sh`. Uses
   `apache/activemq-artemis:2.40` with a tuned 256 MB heap per doc B
   §16.3. Skipped automatically when the host has less than 1 GB free
   (the CI host will run it; the WSL dev box may not).

**Completion criteria:**
- All new tests green.
- AMQP-vs-NATS wire compatibility test passes: same ETIL process
  publishes onto a channel routed to both sinks; a third-party AMQP
  consumer and a third-party NATS consumer both receive a matching
  payload (identical origin tuple).

### Phase 3d — MCP Phase C, broker sources, cycle layer 3 (v2.5.1, commit 3d)

**Tasks:**
1. `mcp_notification_sink` family (doc B §17 Phase C):
   - `src/mcp/mcp_notification_amqp_sink.cpp` bridging
     `etil.mcp.out.notification.**` → AMQP topic.
   - `src/mcp/mcp_notification_nats_sink.cpp` parallel NATS variant.
   - Installed via TIL words `mcp-notify-amqp ( url -- )` /
     `mcp-notify-nats ( url -- )`. Codec is fixed at `"json"` — MCP
     notifications are JSON-RPC by protocol and these words are
     wrappers for that specific use case. Operators who want a binary
     codec on MCP traffic install the underlying `channel-tap-{nats,amqp}`
     directly against `etil.mcp.out.notification.**`. Optional per
     deployment.
2. Broker **sources** (new):
   - `src/manifold/nats_source.cpp` — subscribes to a NATS subject and
     re-publishes inbound messages onto the local ChannelService.
     Parses the incoming `MessageOrigin` from headers (falling back to
     payload) so doc B §20.3 origin echo suppression has identity to
     compare against.
   - `src/manifold/amqp_source.cpp` parallel.
   - TIL: `channel-source-nats ( url codec subject pattern -- handle )`,
     `channel-source-amqp ( url codec address pattern -- handle )`.
     `codec` is the decoder applied to inbound wire bytes — same four
     names as the tap words. Must match the codec the publisher used;
     mismatch produces a decode-error audit on `etil.logging.error`.
3. Cycle-detection **layer 3** (doc B §20.3):
   - In `DefaultChannelService::publish`, add origin-echo check: if
     an incoming message's `(hostname, app_startup_us)` matches this
     process's identity and `reject_own_origin` is set on the
     originating route, drop and emit audit on
     `etil.aaa.audit.channel.echo-dropped`.
   - Configurable via a new field on `RouteSpec`:
     `bool reject_own_origin = true`.
   - `echo=true` tag added to kept echoes when `reject_own_origin` is
     explicitly false (the A → B → A shared-state case).
4. Unit tests:
   - `test_manifold_mcp_broker_sink.cpp` — `sys-notification` publishes
     onto the broker, round-trips through an in-process broker source,
     lands in a capture sink.
   - `test_manifold_echo_suppression.cpp` — same-origin loop triggers
     `echo-dropped` audit; flipping `reject_own_origin` = false delivers
     the echo with `echo=true` tag. Covers doc B §20.9 "broker
     integration test" bullet.
5. Docker E2E: extend `tests/docker/test_manifold_nats.sh` with an
   echo-suppression scenario (publish, subscribe same subject, assert
   one delivery not two).

**Completion criteria:**
- All three cycle-detection layers demonstrably active and
  independently testable (layers 1-2 from Phase 1a, layer 3 now).
- New TIL words present in `data/help.til`.
- `role-network-sink!` gate exercised in at least one negative test.

### Commit arc (Phase 3)

```
<sha> v2.5.1 Phase 3a — broker-sink abstraction + CMake gates + superbuild deps
<sha> v2.5.1 Phase 3b — nats_sink + channel-tap-nats + NATS E2E test
<sha> v2.5.1 Phase 3c — amqp_sink + channel-tap-amqp + Artemis E2E test
<sha> v2.5.1 Phase 3d — MCP Phase C + broker sources + cycle layer 3
```

---

## Phase 4 — Source migration (v2.5.2)

**Design anchor:** doc B §12 Phase 4, §22.3 A-2, §8 ("Sources to wire
up"). Every migration must preserve existing TIL behavior — old direct
transports stay attached as sinks on the new channel so nothing
regresses. Commit per lettered sub-task under patch v2.5.2.

### Phase 4a — ExecutionContext::out_ as channel source (v2.5.2, commit 4a)

**Tasks:**
1. Wrap `ExecutionContext::out_` (the output-stream abstraction used by
   `.`, `emit`, `cr`, `type`, etc.) so every write publishes onto
   `etil.repl.stdout` with payload `std::string` and tags
   `{output_kind: "stdout"|"cr"|"emit"}`.
2. Register a default `etil.repl.stdout` route whose sink is the legacy
   direct-write transport. No observable behavior change in the REPL
   or MCP `interpret` response.
3. The sink is a new `ExecutionOutputSink` that adapts to whatever
   `out_` target is set (string buffer for MCP, ostream for REPL,
   TUI-chunked for the TUI). The adapter lives in
   `src/core/execution_output_sink.cpp`.
4. Unit tests: `test_execution_context_channels.cpp` — verify
   `etil.repl.stdout` receives a message for every character/line the
   interpreter writes, and that disabling the default route gives the
   legacy behavior of capture-to-string.

**Completion criteria:**
- All existing REPL / MCP tests still pass unchanged.
- A TIL integration test can `channel-subscribe` to `etil.repl.stdout`
  and observe its own output as messages (useful for self-introspecting
  benchmarks).

### Phase 4b — MCP request handlers as channel source (v2.5.2, commit 4b)

**Tasks:**
1. Instrument `McpServer::dispatch_request` to publish lifecycle events:
   - `etil.mcp.request.received` at dispatch entry.
   - `etil.mcp.request.completed` at success return.
   - `etil.mcp.request.failed` at error return.
   Payload is a `HeapMap` with `method`, `session_id`, `latency_us`,
   and `error` (failed only).
2. Wire `initialize` / `shutdown` session-lifecycle events to
   `etil.mcp.session.opened` / `.closed` parallel to the existing
   internal logging.
3. The existing `spdlog` log lines become **additional sinks** on these
   channels, not replacements. The named `etil.mcp` logger (from
   Phase 0) subscribes to `etil.mcp.request.**` as a spdlog_sink.
4. Unit tests: `test_mcp_request_channels.cpp` — capture the lifecycle
   sequence for a happy-path and error-path request; assert ordering
   and presence of all tags.

**Completion criteria:**
- Existing `etil.mcp` logger output unchanged in format.
- A channel subscriber can reconstruct the MCP request timeline from
  `etil.mcp.request.**` alone.

### Phase 4c — Evolution engine as channel source (v2.5.2, commit 4c)

**Tasks:**
1. Instrument the evolution engine's public event boundaries to publish:
   - `etil.evolution.generation.start` at generation loop entry.
   - `etil.evolution.generation.end` at exit with fitness summary.
   - `etil.evolution.mutation.applied` at each substitute/grow/shrink.
   - `etil.evolution.fitness.evaluated` per candidate with score.
   Payloads are `HeapMap`s with the relevant scoring / topology data.
2. Channels are independent of EvolveLogger — that absorption is 4d.
   Until 4d lands, 4c and EvolveLogger co-exist: the engine publishes
   onto channels **and** calls the existing `EvolveLogger::inst().log*()`
   wrappers.
3. Unit tests: `test_evolution_channels.cpp` — short `evolve-chain`
   benchmark with a `channel-subscribe` subscriber; assert the expected
   event sequence and fitness-score presence.

**Completion criteria:**
- No change in evolution benchmark output or log files.
- New channels deliver end-to-end.

### Phase 4d — EvolveLogger absorption (v2.5.2, commit 4d)

**Tasks:**
Resolves doc B §22.3 ambiguity A-2 per the pre-decided mechanics in
§Ambiguities above.

1. Map the `EvolveLogCat` mask bits 1:1 to subchannels:
   - `CAT_FITNESS` → `etil.evolution.fitness`
   - `CAT_MUTATION` → `etil.evolution.mutation`
   - `CAT_GENERATION` → `etil.evolution.generation`
   - `CAT_ERROR` → `etil.evolution.error`
   - *(add rows for every existing category — enumerated during
     implementation from the `EvolveLogCat` enum).*
2. Rewrite `EvolveLogger` as a thin adapter whose `log*()` methods
   construct a `Message` and call `channels_->publish()`. Mask check
   becomes `channels_->has_routes(channel)` — preserves the zero-cost
   short-circuit from doc B §14 Q5.
3. The existing evolution log **file** becomes a default route:
   `file_sink(<path>)` on `etil.evolution.**` pattern, installed at
   ChannelService init alongside the existing hardwired channels (doc
   B §15.5 table). Path and rotation come from the current
   `AuthConfig`-like settings.
4. Migrate the 105 direct `EvolveLogger` call sites (per doc B §22.3
   A-2). Mechanical: replace `EvolveLogger::inst().log_fitness(...)`
   with the equivalent `channels->publish(Message{channel: "etil.evolution.fitness", payload: ...})`.
   Most sites fit a helper macro defined in `include/etil/evolution/log_compat.hpp`.
5. Delete the `EvolveLogger` header + impl files at the end of the
   sub-task once the helper macro is in place. Keep the four
   `evolve-log-*` TIL words (`evolve-log-open`, `-close`, `-flush`,
   `-stats`) as thin wrappers over the new channel routes so existing
   benchmark scripts still work.
6. Unit tests: `test_evolvelogger_absorption.cpp` — parity test asserting
   the file produced by the new `file_sink` on `etil.evolution.**`
   matches byte-for-byte (modulo timestamps) the file produced by the
   old `EvolveLogger` for an identical `evolve-chain` run. Run both
   before and after the migration on the same random seed
   (`evolve-seed! 42`).

**Completion criteria:**
- `EvolveLogger` class deleted.
- All 105 call sites migrated; `scripts/check-logging-policy.sh` still
  passes (check not triggered because EvolveLogger never used direct
  stdio, but logic is preserved).
- Evolution benchmark output byte-identical on seeded runs (the parity
  test above).
- The four `evolve-log-*` TIL words still execute and return the same
  shapes they did on v2.4.3.

### Commit arc (Phase 4)

```
<sha> v2.5.2 Phase 4a — ExecutionContext::out_ → etil.repl.stdout source
<sha> v2.5.2 Phase 4b — MCP request handlers → etil.mcp.request.** source
<sha> v2.5.2 Phase 4c — Evolution engine → etil.evolution.** source
<sha> v2.5.2 Phase 4d — EvolveLogger absorbed; 105 call sites migrated
```

---

## Integration validation (v2.5.2, no version bump)

Runs after all phase commits land. Single commit, no version bump.

### Tests

1. **End-to-end broker round-trip** —
   `tests/docker/test_manifold_broker_e2e.sh`. Start `nats-server` and
   Artemis containers side-by-side with etil. Publish onto a channel
   routed to both sinks. Assert:
   - Both brokers receive the payload.
   - A third-party subscriber on each broker reconstructs the full
     `MessageOrigin` from headers.
   - Echo suppression: re-subscribe on the same broker; assert
     `echo-dropped` audit fires and the second copy is not delivered
     locally.
2. **Source migration regression** — run the existing unit + TIL test
   suites unchanged. All 1601+N tests must pass where N is the new
   test count. Any legacy test failure is a migration bug.
3. **Evolution parity** — `evolve-seed! 42` + a fixed short benchmark,
   compare fitness log output before (v2.4.3 saved log) and after
   (v2.5.2 new log) for byte equality modulo timestamps. Expected to
   pass because Phase 4d explicitly preserved file-sink semantics.
4. **Memory footprint** — `docker stats` snapshot with NATS + Artemis
   + etil containers running. Must fit the 961 MB CI-host RAM budget.
   If Artemis + etil alone exceeds budget, document the finding and
   mark Artemis as "LAN-only, not co-located" in the sprint retrospective.
5. **Logging policy** — `scripts/check-logging-policy.sh` passes.
   The allowlist stays at the three bootstrap sites from Phase 0; no
   new additions.

### Commit

```
<sha> Integration validation (v2.5.2) — broker E2E, source regression, evolve parity
```

---

## Documentation updates (v2.5.2, no version bump)

Per workflow doc §4, documentation is last. Single commit.

### Files

1. **`README.md`** — two changes:

   a. **Existing "Manifold" section** — add broker-sink subsection;
      update the word count; cross-link to the new appendix.

   b. **New appendix: "Appendix: Manifold — I/O Channel Pipeline"**
      — the full user-facing reference for every word shipped across
      Phases 0–4. Added at the end of README.md. Structure:

      1. **Introduction — what Manifold is and why.** 3-5 paragraphs:
         - The problem Manifold solves (N-subscriber fanout, diagnostic
           routing, cross-process event transport, MCP SSE as a
           channel endpoint) — paraphrased from doc B §1 "The shape of
           the problem."
         - Core concepts in one paragraph each: channel name grammar
           (dotted, `*` / `**` wildcards), `Message` identity tuple
           `(hostname, app_startup_us, session_id, seq, origin_type)`,
           route = pattern + transforms + sink, RBAC gating, three
           cycle-detection layers.
         - Use cases (one paragraph each):
           - **Live debugging** — `channel-tap-file` on
             `etil.evolution.**` during a benchmark.
           - **Multi-consumer fanout** — broker sink so TUI + archiver
             + metrics collector share one ETIL publish.
           - **MCP notifications** — `sys-notification` /
             `user-notification` flow through
             `etil.mcp.out.notification.**` to SSE + optional broker.
           - **Cross-process correlation** — origin tuple plus
             `Session-Hmac` header lets a fleet of ETIL processes
             produce a merge-sorted audit trail on a shared broker.
           - **In-TIL observation** — `channel-subscribe` returns an
             observable that evolution benchmarks can chain through
             existing observable operators (`take`, `filter`, `map`).
           - **Security auditing** — hardwired `etil.aaa.audit.**` +
             `etil.security.**` with inline delivery and bypass of
             the master-off switch.

      2. **Word reference.** Every Manifold word documented with:
         stack effect signature, full description, RBAC gate (if any),
         and a runnable TIL example. Group by topic (same grouping as
         doc B §21):

         - **Core publish / route / list**: `channel-publish`,
           `channel-route-add`, `channel-route-remove`, `channel-list`,
           `channel-list-routes`, `channel-tap-file`.
         - **Observable bridge**: `channel-subscribe`,
           `channel-tap-observable`.
         - **Identity**: `channel-origin`, `channel-seq`,
           `channel-last-published`.
         - **Cycle / drop visibility**: `channel-trace`,
           `channel-hops-left`, `channel-cycle-stats`,
           `channel-sink-stats`, `channel-all-sink-stats`.
         - **Permissions**: `channel-perm-check`, `channel-perm-list`.
         - **Role admin**: `role-grant-channel`,
           `role-revoke-channel`, `role-channel-enable!`,
           `role-network-sink!`.
         - **MCP inbound**: `mcp-on-notification`, `mcp-on-progress`,
           `mcp-on-cancelled`, `mcp-on-roots-changed`,
           `mcp-on-request`.
         - **Broker sinks / sources (new, Phase 3)**:
           `channel-tap-nats`, `channel-tap-amqp`,
           `channel-source-nats`, `channel-source-amqp`,
           `mcp-notify-nats`, `mcp-notify-amqp`.
         - **Session hashing (new, Phase 3)**: `channel-session-hmac`.

         Every example must be runnable against a default-configured
         `etil-repl` — integration-test it during documentation
         commit by piping the README appendix's code blocks through
         `etil-repl --pipe` and asserting no errors (a new CI
         sub-check; see Phase 3a test list addition).

      3. **Worked scenario walkthroughs.** Four end-to-end TIL
         scripts, each with inline commentary:

         - **Scenario A — Single-process live tail.** Subscribe to
           `etil.evolution.**`, run a short `evolve-chain`, observe
           fitness events. No broker, no RBAC configured. Shows the
           minimum useful Manifold setup.
         - **Scenario B — Multi-subscriber NATS fanout.** Two
           external `nats` subscribers; ETIL publishes once;
           walkthrough covers connect, header inspection
           (`Session-Hmac` + `MessageOrigin`), codec selection
           (`json` for TUI, `msgpack` for archiver), and echo
           suppression with `reject_own_origin`.
         - **Scenario C — MCP notification over broker.** `sys-notification`
           in TIL; same notification arrives at a remote TUI via
           SSE **and** a Python archiver via NATS. Explains why the
           MCP channel namespace (`etil.mcp.out.notification.**`)
           cleanly supports both transports without either side
           caring.
         - **Scenario D — RBAC lockdown.** `role-channel-enable!`
           off for a restricted role; hardwired audit/security
           channels still emit; demonstrates the master-off bypass.
           Cross-references doc B §15 for the full decision
           procedure.

      4. **MQ key / wire-format structures.** One subsection per
         broker transport, documenting exactly what arrives on the
         wire. This is the reference readers need when integrating a
         non-ETIL consumer:

         - **NATS wire format.**
           - Subject: channel name 1:1 (`etil.evolution.fitness`).
           - Headers (NATS message headers):
             - `Session-Hmac`: base64url-encoded 128-bit
               HMAC-SHA256 of the originating session_id under the
               producer's process key. Empty when no session is
               bound.
             - `Msg-Host`: producer hostname.
             - `Msg-Startup`: producer `app_startup_us` as decimal
               string.
             - `Msg-Seq`: producer sequence number as decimal.
             - `Msg-OriginType`: `native` or `browser`.
             - `Msg-Codec`: `json` | `msgpack` | `cbor` | `raw`.
             - `Msg-RouteTrace`: comma-joined list of channel hops
               (for cross-broker cycle debugging).
             - `Msg-HopsLeft`: remaining TTL as decimal.
           - Payload: codec-encoded payload bytes. When codec is
             `json` the bytes are UTF-8 JSON with the full
             envelope `{channel, payload, tags, origin}` (same
             shape `json_encoder` produces for in-process file
             sinks). When codec is `msgpack` / `cbor` / `raw`
             the envelope structure is preserved as a map/object
             but encoded per the codec.

         - **AMQP 1.0 wire format.**
           - Address: channel name 1:1, translating `**` → `#` and
             `*` → `*` at broker-subscribe time.
           - Message properties:
             - `subject`: original channel name (pre-translation).
             - `content-type`: `application/json`,
               `application/msgpack`, `application/cbor`, or
               `application/octet-stream` depending on codec.
           - Application properties (same set as NATS headers
             above, same names for subscriber portability).
           - Body: codec-encoded payload bytes.

         - **Envelope JSON schema (codec = `json`).** Full JSON
           Schema for the payload envelope:

           ```
           {
             "channel": "etil.evolution.fitness",
             "payload": /* arbitrary JSON value per channel */,
             "tags": {"level": "info"},
             "origin": {
               "host": "hostname.example.com",
               "startup": 1745000000000000,
               "session_hmac": "base64url...",
               "seq": 12345,
               "origin_type": "native"
             }
           }
           ```

           Note: `origin.session_hmac` is in the envelope, not the
           raw `session_id`. Local subscribers that need to
           correlate with a known plaintext session_id use
           `channel-session-hmac` to hash and compare.

         - **Wire-format compatibility matrix.** Table showing
           which fields are required / optional, which are stable
           across releases, and which are informational-only. Feed
           this table from the `BrokerSink` schema constants so it
           cannot drift from the code — a small cmake-time
           generator script produces the markdown.

      5. **Appendix footer — pointers to design docs.** Link doc A,
         doc B, and this implementation plan for readers who want
         the why rather than the how.

2. **`data/help.til`** — help entries for every new TIL word added in
   Phases 3 and 4. The one-line summary in `help.til` mirrors the
   stack-effect line from the README appendix; the appendix is the
   long-form reference.

3. **`CLAUDE.md` (project root)** — update the "Manifold" section to
   mention broker sinks as shipped (was "Phase 3+" at v2.4.3). Add
   note that `EvolveLogger` is absorbed into `etil.evolution.**`.
   Cross-link the new README appendix.

4. **`docs/claude-design/20260418B-IO-Channel-Pipeline-Architecture.md`**
   — update §22.3 resolution log:
   - A-2 → RESOLVED (Phase 4d, commit 4d at v2.5.2)
   - A-5 → RESOLVED (Phase 3c, see Ambiguities above)

5. **`docs/claude-design/20260418A-Logging-Infrastructure-Survey.md`**
   — footnote noting EvolveLogger absorption completed.

### Appendix-size estimate and review gate

The README appendix is expected to run 1500–2500 lines — it is the
primary user-facing reference for Manifold. Budget for it
accordingly when drafting the documentation commit. Before pushing,
at least one scenario walkthrough (preferably Scenario B, the most
complex) must be executed end-to-end against a running ETIL +
nats-server pair and the captured output pasted as a verification
block in the PR description.

### Commit

```
<sha> Manifold Phase 3-4 docs — README, help.til, CLAUDE.md, doc B §22.3 updates
```

---

## Merge and push

Per workflow doc §5-6. **Ask for permission before each step.**

1. `ETIL_GIT_REMOTE=<ci-remote> scripts/super-push.sh --message "Feature: Manifold broker sinks + source migration (Phases 3-4)"`
   (substitute the configured CI git remote name).
2. Wait for CI. Expected pipeline duration: ~20-25 min (adds Artemis +
   NATS E2E on top of the 16 min Phase 0-1-2 baseline).
3. On CI green, ask permission, run `scripts/github-push.sh`.

Per the user-memory rule: push to the CI remote first, GitHub second. Never
the reverse.

---

## Speed bumps to observe

Per `docs/claude-knowledge/20260404-speedbumps.md`:

- **No compound bash.** No `cd X && Y` — use `git -C`, project scripts,
  absolute paths. Write `/tmp/foo.sh` and run it instead of chaining.
- **No command injection.** No `ctest -j$(nproc)` — run `nproc` first,
  substitute manually, or use `scripts/test.sh`.
- **`tee` long processes to `/tmp`.** The Artemis + NATS CI build will
  take many minutes — always `tee` build output to `/tmp/<timestamp>.log`
  before `grep`ing or `tail`ing, so a missed line doesn't force a
  re-run.
- **TIL coding traps.** Only `#` for comments (never `(`). Control flow
  (`if`/`do`/`begin`) only inside colon definitions. These catch out the
  TIL E2E test scripts in `tests/docker/`.
- **Write `.til` files, don't heredoc.** Per user-memory
  `feedback_til_not_heredocs`: for anything non-trivial in the Docker
  E2E scripts, write to `/tmp/<name>.til` and `include` it rather than
  embedding TIL inside bash heredocs.

---

## Risks and mitigations

| Risk                                                    | Mitigation                                                                                                           |
| ------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Artemis footprint exceeds the CI host RAM                    | Mode B in doc B §16.7 — Artemis runs on a separate host in production; CI can still exercise it on a richer runner. |
| NATS dispute re-emerges post-settlement                 | Ambiguity pre-resolved at Apache 2.0 licensing; fall back to Artemis-only is a config change, not a re-plan.         |
| EvolveLogger parity test fails on seed 42               | Preserve `EvolveLogger` class as a thin adapter for one extra release; delete only after parity proven across three benchmarks. |
| 105 call-site migration introduces a subtle regression  | Phase 4c publishes to channels **before** Phase 4d deletes EvolveLogger — two-week overlap proves both paths work.   |
| Qpid Proton-C++ superbuild breaks WSL developer loop    | Flag is OFF by default in CMake; WSL devs can ignore it. CI runs with flag ON.                              |
| Cycle-detection layer 3 drops legitimate A→B→A traffic  | Explicit opt-out: `reject_own_origin = false` on the `RouteSpec` keeps the echo with `echo=true` tag.                |

---

## Dependencies

**New third-party libraries** (both Apache 2.0, both license-compatible
per doc B §16.9):

- `nats.c` — NATS C client, pinned to latest 2025 release.
- Qpid Proton-C++ — AMQP 1.0 reactor, pinned to a stable Apache release.

**Existing ETIL components used:**

- Phase 0 logging substrate (`etil::core::logging`).
- Phase 1 Manifold core (`ChannelService`, `Message`, `ISink`,
  `RouteSpec`, RBAC).
- Phase 2 observable bridge (for subscription-based broker sources).
- `ExecutionContext` — target of Phase 4a wrap.
- `McpServer::dispatch_request` — target of Phase 4b instrumentation.
- `EvolveLogger` — target of Phase 4d absorption and deletion.

No new runtime dependencies required for Mode C (broker-disabled)
builds. The WSL developer loop is unchanged.

---

## Completion checklist

```
[ ] Pre-flight: master clean, CI green at v2.4.3
[ ] Feature branch created (branch.sh manifold-broker-and-sources)
[ ] Ambiguity A-5 resolved (pre-Phase-3c)
[ ] Ambiguity A-2 resolved (pre-Phase-4d)
[ ] Broker-payload codec default resolved (pre-Phase-3a)
[ ] Phase 3a — broker-sink abstraction + CMake gates — committed
[ ] Phase 3b — nats_sink + E2E test — committed
[ ] Phase 3c — amqp_sink + Artemis E2E test — committed
[ ] Phase 3d — MCP Phase C + broker sources + cycle layer 3 — committed
[ ] Phase 4a — ExecutionContext::out_ source — committed
[ ] Phase 4b — MCP request-handler source — committed
[ ] Phase 4c — evolution engine source — committed
[ ] Phase 4d — EvolveLogger absorbed, 105 sites migrated — committed
[ ] Integration validation — committed
[ ] Documentation updated (README, help.til, CLAUDE.md, doc B §22.3)
[ ] Permission granted — run super-push.sh
[ ] super-push.sh — built, tested, merged, tagged, pushed to CI remote
[ ] Feature branch deleted (automatic via super-push.sh)
[ ] CI — build + broker E2E passed
[ ] Permission granted — push to GitHub
[ ] github-push.sh — complete
```

---

## Post-sprint

Expected open items after v2.5.2 ships:

- **Phase 5** (Android-Intent layer per doc B §11) — still deferred
  until concrete cross-subsystem dispatcher need materializes.
- **Kafka / Pulsar sinks** — deferred until multi-tenancy or
  streaming-platform features justify the footprint (doc B §16.5, §16.8
  Phase N).
- **Dynamic role-admin persistence** — own sprint; converts Phase 2d's
  session-scoped grants into AuthConfig / MongoDB-backed grants.
- **etil-web Phase 3b** — browser WebSocket / broadcast sinks in the
  separate `etil-web` repo. Can start in parallel; this sprint's C++
  API is stable.
- **GET /mcp SSE integration test** — the unit-level test that hung on
  httplib's `stop()` in the Phase 2d sprint. Add under `tests/docker/`
  alongside the existing MCP HTTP E2E coverage. Not blocking this
  sprint, but low-hanging once the broker E2E harness exists.
