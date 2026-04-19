# Manifold — ETIL I/O Channel Pipeline Architecture

**Date:** 2026-04-18
**Status:** Design exploration (no code yet)
**Library moniker:** Manifold — the splitter/combiner of streams, after the
fluid-engineering component that routes one inlet to many outlets (or the
reverse). Library name `libetil-manifold`, namespace `etil::manifold`.
TIL verbs remain the abstraction-neutral `channel-*` (`channel-publish`,
`channel-subscribe`, etc.) — the moniker names the library, the
abstraction stays "channel."
**Companion:** `20260418A-Logging-Infrastructure-Survey.md` (logging substrate)

---

## 0. TL;DR

Build **Manifold** — a separate static library (`libetil-manifold`)
providing a multi-source → multi-sink dataflow system with composable
transforms.
Every diagnostic, language-I/O, evolution-tracer, and MCP-event stream in
ETIL becomes a **named channel** produced by a **source** and consumed by
zero or more **sinks** through zero or more **transforms**. Routing is
loose-coupled, Android-Intent style. Observables are the in-process
transport and the TIL control surface. Services are consumed via
dependency injection, never global singletons.

The logging module from survey §8 is the bottom layer; this library is the
next layer up. REPL output, MCP server stdout, evolution fitness traces,
MongoDB warnings — all become streams on this same substrate.

---

## 1. The shape of the problem

A logging library answers "how do I record a string somewhere." A channel
pipeline answers a different and larger question: "how do I route any typed
event from any producer to any consumer, with configurable filtering,
enrichment, and transport, without either end knowing about the other."

Concrete scenarios the logger alone cannot satisfy:

1. **Redirect REPL stdout to UDP.** Point a socket at a collector, rewire
   `ExecutionContext::out_` to publish onto `etil.repl.stdout`, attach a
   `udp_sink` to that channel. Zero changes to TIL code, zero changes to
   the interpreter.
2. **Live tail of MCP request flow in the TUI.** The TUI subscribes to
   `etil.mcp.request.*` via an MCP notification sink; messages are filtered
   to the authenticated user's session and streamed to the UI.
3. **Evolution benchmark trace capture.** A script attaches a `file_sink` +
   `json_encoder` transform to `etil.evolution.**`, runs the benchmark,
   detaches. The captured transcript is replayable into a test harness.
4. **Fitness landscape annotator.** A `tag_annotator` transform attaches
   `{concept: sqrt-approx, generation: 47}` to every fitness event passing
   through `etil.evolution.fitness`; downstream sinks index by tag.
5. **Rate-limited warning flood protection.** Falco-style "alert storm"
   prevention: a `rate_limiter` transform on `etil.mcp.auth.failure`
   throttles to 1/sec, forwards a count summary hourly.
6. **Test-time capture.** Unit tests inject a `MockChannelService` that
   collects all messages into a vector; assertions match against the vector.
   The system-under-test is oblivious.

Each is a composition of the same three nouns: **source**, **transform**,
**sink**, connected by named **channels**.

---

## 2. Vocabulary

- **Message** — typed envelope: `{channel, timestamp, tags, payload}`.
- **Channel** — named routing identifier (e.g., `etil.mcp.request.received`).
  Uses dotted namespaces; wildcards match subtrees (`etil.mcp.**`).
- **Source** — anything that calls `channel.publish(msg)`.
- **Sink** — anything that implements `accept(const Message&)`.
- **Transform** — a function `Message → optional<Message>` (or
  `Message → vector<Message>` for fan-out). Returns `nullopt` to drop.
- **Route** — a declaration wiring a channel pattern to a sink through an
  ordered list of transforms.
- **Router** — the service that accepts `publish` calls and dispatches to
  matching routes.
- **Intent** (upper layer) — a typed, semantically-named event
  (`intent.etil.evolution.generation_complete`) — richer than a raw channel
  message, handled by declarative intent filters.

---

## 3. Core types (C++ sketch)

```cpp
namespace etil::manifold {

struct MessageOrigin {
    std::string_view hostname;        // interned, process-global static
    int64_t  app_startup_us;          // UTC microseconds at ChannelService init
    std::string session_id;           // empty for system / pre-session messages
    int64_t  seq;                     // per-process atomic counter; strictly increasing
};

struct Message {
    MessageOrigin origin;                                  // globally unique tuple — §18
    std::string channel;                                   // routing key
    std::chrono::system_clock::time_point timestamp;       // publish time (not identity)
    absl::flat_hash_map<std::string, std::string> tags;    // session, user, level
    std::any payload;                                      // sinks cast by contract
    std::type_index payload_type;                          // for runtime dispatch

    // Cycle detection — §20
    absl::InlinedVector<std::string, 4> route_trace;       // channels this message has visited
    uint8_t hops_remaining = 32;                           // TTL-style hop budget
};

class ISink {
public:
    virtual ~ISink() = default;
    virtual void accept(const Message&) = 0;
    virtual void flush() {}
};

class ITransform {
public:
    virtual ~ITransform() = default;
    virtual std::vector<Message> apply(Message) = 0;       // 0..N output
};

class ISource {
public:
    virtual ~ISource() = default;
    // Sources publish by calling ChannelService::publish(msg).
    // The interface exists mostly for lifetime/registration.
};

struct RouteSpec {
    std::string channel_pattern;                           // "etil.mcp.**"
    absl::flat_hash_map<std::string, std::string> tag_filter;
    std::vector<std::shared_ptr<ITransform>> transforms;
    std::shared_ptr<ISink> sink;
};

class ChannelService {
public:
    virtual ~ChannelService() = default;
    virtual void publish(Message) = 0;
    virtual RouteHandle add_route(RouteSpec) = 0;
    virtual void remove_route(RouteHandle) = 0;
    virtual std::shared_ptr<etil::core::HeapObservable>
        observe(const std::string& channel_pattern) = 0;  // TIL-facing
};

} // namespace etil::manifold
```

Key decisions sketched here:

- **`std::any` payload** — type-erased so the channel is polymorphic over
  message types. Sinks that care about the payload type check
  `payload_type` and cast; sinks that treat it opaquely (UDP, file) use a
  `to_string()` transform upstream.
- **Abseil hash maps** — already a project dependency, no new library.
- **Observable bridge** — `observe(pattern)` returns a `HeapObservable`
  that fires for each matching message. This is the TIL entry point.

---

## 4. Built-in sinks

| Sink | Purpose |
|---|---|
| `spdlog_sink` | Forward to a named spdlog logger (the survey §8 substrate) |
| `file_sink` | Direct rotating or per-run file, bypasses spdlog for high-throughput |
| `stderr_sink` | For the three bootstrap exceptions (survey §11.2) |
| `udp_sink` | Send to a remote collector on UDP — the user's motivating example |
| `tcp_sink` | Reliable delivery to a remote collector |
| `observable_sink` | Push onto a `HeapObservable` so TIL code / the TUI can subscribe |
| `ring_buffer_sink` | Bounded in-memory tail (debug dump, crash context) |
| `mcp_notification_sink` | Emit as MCP protocol notifications to connected clients |
| `test_capture_sink` | Collect into a vector for unit-test assertions |
| `null_sink` | Discard (used for disabled routes without removing them) |

---

## 5. Built-in transforms

| Transform | Purpose |
|---|---|
| `level_filter` | Drop messages below configured severity |
| `channel_filter` | Match channel against a regex/glob, drop non-matches |
| `tag_filter` | Require specific tag key/value, drop non-matches |
| `tag_annotator` | Inject constant tags (session_id, user_id, hostname) |
| `tag_from_context` | Pull tags from thread-local context |
| `rate_limiter` | Token-bucket drop-tail; emit periodic "dropped N" summary |
| `batcher` | Coalesce N small messages or T-second window into one batch |
| `debouncer` | Suppress duplicates within a window |
| `formatter` | Convert typed payload → string for line-based sinks |
| `json_encoder` | Convert message → JSON bytes for UDP/TCP |
| `fan_out` | Duplicate message to multiple downstream transforms |
| `sampler` | Keep 1-in-N messages (high-volume trace thinning) |

Transforms are pure and thread-safe. They compose left-to-right in the
`RouteSpec::transforms` vector. Each transform may emit zero or more
messages; downstream transforms see the output of upstream transforms.

---

## 6. Observables as the in-process transport

ETIL already has `HeapObservable` with primitives for subscribe/emit. The
channel library uses observables as its internal push mechanism for
in-process routes:

- Each channel has a backing `HeapObservable<Message>`.
- Sinks are subscribers on that observable.
- Transforms are implemented as observable operators
  (`map`, `filter`, `buffer`) that return new observables.
- External transports (UDP, TCP, file) are terminal subscribers that
  serialize and emit outside the process.

This means **TIL code can participate as both source and sink** using the
existing observable primitives — no new TIL machinery to add
observable-style subscription. The channel system is a typed, routed
overlay on top of observables.

---

## 7. TIL control surface

Words for runtime channel management, by analogy with `evolve-log-*`:

```
channel-publish        ( msg-str channel-name -- )
channel-subscribe      ( channel-pattern -- observable )
channel-route-add      ( route-spec -- handle )
channel-route-remove   ( handle -- )
channel-tap-file       ( channel-pattern path -- handle )    # shortcut
channel-tap-udp        ( channel-pattern host port -- handle )
channel-tap-observable ( channel-pattern -- observable )     # shortcut
channel-list-routes    ( -- array )
channel-list-channels  ( -- array )
```

Benchmark scripts, debugging sessions, and long-running MCP clients can
reconfigure the pipeline at runtime without rebuilding. The tap-* words
are sugar over `channel-route-add` for the common case of "send this
channel somewhere durable for a while."

---

## 8. Sources to wire up

The migration is gradual. Each subsystem becomes a source by changing its
output call from a raw stream to `channels->publish(...)`.

| Channel | Current producer | Today's transport |
|---|---|---|
| `etil.repl.stdout` | `ExecutionContext::out_` | `std::cout` |
| `etil.repl.stderr` | `ExecutionContext::err_` | `std::cerr` |
| `etil.mcp.request.received` | `handle_request` entry | (nonexistent) |
| `etil.mcp.request.completed` | `handle_request` exit | (nonexistent) |
| `etil.mcp.request.failed` | exception handler | `fprintf(stderr)` |
| `etil.mcp.session.created` | session pool | (nonexistent) |
| `etil.mcp.session.evicted` | line 270 | `spdlog::info` |
| `etil.db.mongo.error` | `mongo_client.cpp` exceptions | `fprintf(stderr)` ×16 |
| `etil.aaa.audit.*` | `audit_log.cpp` | Mongo collection |
| `etil.oauth.github.*` | `oauth_github.cpp` | `fprintf(stderr)` |
| `etil.oauth.google.*` | `oauth_google.cpp` | `fprintf(stderr)` |
| `etil.evolution.generation` | `evolve_dag_generation` | EvolveLogger DAG |
| `etil.evolution.fitness` | `record_fitness` call sites | aggregated only |
| `etil.evolution.selection` | UCB computation | aggregated only |
| `etil.evolution.mutation.*` | `ast_genetic_ops.cpp` | EvolveLogger |

EvolveLogger becomes a sink on `etil.evolution.**` channels, preserving
its file-per-run semantics. Spdlog loggers become sinks on diagnostic
channels. Nothing about survey §8 is wasted.

---

## 9. Dependency injection

Every consumer takes a `std::shared_ptr<ChannelService>` in its
constructor. No global singletons.

```cpp
class McpServer {
public:
    McpServer(std::shared_ptr<channels::ChannelService> channels,
              std::shared_ptr<auth::AuthService> auth,
              ...);
};
```

At process startup:

```cpp
auto channels = std::make_shared<channels::DefaultChannelService>(
    channels::DefaultConfig{
        .default_routes = load_routes_from_config(),
        .thread_pool_size = 2,
    });

auto mcp = std::make_shared<McpServer>(channels, auth, db);
```

Tests:

```cpp
auto channels = std::make_shared<channels::TestChannelService>();
auto mcp = std::make_shared<McpServer>(channels, /* ... */);

mcp->handle_request(req);

EXPECT_THAT(channels->captured("etil.mcp.request.received"), SizeIs(1));
```

This is textbook DI, nothing exotic. The important property is that
**subsystems do not know whether messages go to file, UDP, or a test
vector** — they only know the channel name.

---

## 10. Library packaging

`libetil-manifold.a` as a separate CMake target. Namespace
`etil::manifold`. Directory layout uses the moniker so the library is
discoverable by name in the tree:

```
include/etil/manifold/
  message.hpp
  origin.hpp
  sink.hpp
  transform.hpp
  service.hpp
  sinks/
    spdlog_sink.hpp
    file_sink.hpp
    udp_sink.hpp
    observable_sink.hpp
    ...
  transforms/
    level_filter.hpp
    tag_annotator.hpp
    rate_limiter.hpp
    ...

src/manifold/
  default_service.cpp
  router.cpp
  sinks/
  transforms/
```

Dependencies:

- `abseil` (already present) — hash maps
- `spdlog` (already present) — via `spdlog_sink`
- `nlohmann/json` (already present) — via `json_encoder` transform
- `libuv` (already present) — for UDP/TCP sinks

No new third-party dependencies. Fits cleanly into the existing CMake.

Link pattern:

- `etil_core` depends on `etil_channels`.
- `etil_evolution` depends on `etil_channels`.
- `etil_mcp` depends on `etil_channels`.
- `etil_repl` depends on `etil_channels`.
- `etil_tui` (separate repo) can link `etil_channels` directly if it
  wants local routing, or receive channels over MCP notifications.
- External tools (benchmarks, analyzers) link `etil_channels` alone.

---

## 11. Android-Intent upper layer (optional, later)

Channels are the **transport**. Intents are a **typed schema** on top.

An Intent:

```cpp
struct Intent {
    std::string action;                       // "etil.evolution.generation_complete"
    std::string category;                     // "monitor" | "persist" | "live"
    absl::flat_hash_map<std::string, std::string> extras;
    std::any data;
};
```

An IntentFilter:

```cpp
struct IntentFilter {
    std::string action_pattern;               // glob against Intent::action
    std::optional<std::string> required_category;
    std::function<bool(const Intent&)> predicate;  // custom match
    std::shared_ptr<IIntentReceiver> receiver;
};
```

Why Intents on top of channels?

- **Typed contracts.** An Intent action defines its extras schema. Channels
  carry anything; Intents carry documented-shape events.
- **Semantic routing.** "Anything that wants to react to a generation
  completing" is a clearer primitive than "subscribe to
  `etil.evolution.generation` and filter on tag `event=complete`."
- **Cross-process dispatch.** Intents can be serialized and forwarded
  across MCP to other ETIL clients or external listeners — a single named
  handler API regardless of where the listener lives.
- **Declarative wiring.** Intent filters can be specified in config files
  or TIL words, not hard-coded routes.

Intents are a **future concern**, not part of the initial library. The
channel system can ship first and prove itself; Intents layer on top once
we have concrete cross-subsystem event types to codify.

---

## 12. Phasing

1. **Phase 0 — Logging substrate.** Do survey §8: the `logging` module,
   migrate the 41 stderr sites, policy enforcement. Independently useful.
   Does not require this library.
2. **Phase 1 — Manifold core.** `libetil-manifold.a` with
   `ChannelService`, `Message`, `ISink`, `ITransform`, `Router`,
   `DefaultChannelService`, the spdlog/file/stderr/test-capture sinks,
   and the level/tag/format transforms. No TIL integration yet.
3. **Phase 2 — Observable bridge.** `observable_sink` and `observe()` API,
   TIL words `channel-subscribe` / `channel-publish`. Makes the pipeline
   scriptable from benchmark TIL files.
4. **Phase 3 — Network sinks.** `udp_sink`, `tcp_sink`, `mcp_notification_sink`.
   Enables the remote-collector and live-tail scenarios.
5. **Phase 4 — Source migration.** Wrap `ExecutionContext::out_`, MCP
   request handlers, evolution engine, EvolveLogger as channel sources.
   Old direct transports remain as sinks on the respective channels so
   nothing regresses.
6. **Phase 5 — Intent layer** (if/when the need materializes). Typed
   intent dispatch on top of the channel transport.

Phases 0 and 1 are independently useful and can land without committing
to the full roadmap. Everything after is opportunistic — wire up what the
work needs.

---

## 13. Open questions

- **Message lifetime / copy cost.** `std::any` copies on publish. For
  high-rate evolution traces this may matter. Options: `shared_ptr<const
  Message>` semantics, moveable messages, a small-buffer optimization for
  common payload sizes. Benchmark before committing.
- **Backpressure.** Resolved in §22 Answer A1: fixed-size ring buffer
  (default 16 entries) per route, **default drop-first** (drop oldest
  to make room for newest on overflow), optional `drop_last` mode for
  audit-style retention. **Never block the producer.** Hard-wired audit
  and security channels bypass the ring entirely and deliver inline on
  the producer thread to avoid any drop path — see §22.2.
- **Threading model.** Sinks run on a shared thread pool? One thread per
  slow sink? Inline on the producer thread? Probably a mix, configured
  per-sink. File sinks inline (fast); network sinks on the pool.
- **Schema evolution.** How does a consumer handle a new tag or a changed
  payload type? Backwards-compatible tag set + versioned payload types
  probably, but this needs thought before Intents land.
- **Security.** Resolved in §15 "RBAC for channels" below. `udp_sink` /
  `tcp_sink` and every other channel operation are gated by per-role
  permissions layered on the existing `RolePermissions` struct. Network
  sinks additionally inherit the rules in
  `20260214A-ETIL-Server-Security-Rules.md` (sandboxing, allowlist).
- **Overhead when disabled.** Zero-cost when a channel has no routes —
  short-circuit at `publish()` entry. Same philosophy as EvolveLogger's
  `enabled(cat)` check.

---

## 14. Decision points before coding

Before Phase 1 starts, agree on:

1. **`std::any` vs a closed payload type hierarchy.** Closed hierarchy is
   faster and more typesafe; `std::any` is open-ended and friendly to
   external tools. Recommendation: start with `std::any`, benchmark,
   specialize if needed.
2. **Channel name conventions.** Dotted with `etil.<module>.<event>.<detail>`
   probably. Nail down before subsystems start publishing — renames later
   are painful.
3. **Whether EvolveLogger stays separate.** It could become purely a set
   of sinks on `etil.evolution.**` channels, deleting the EvolveLogger
   class. Cleaner long-term but disruptive — the survey doc recommends
   keeping it as-is for now. Revisit when Phase 4 lands.
4. **DI framework.** Raw constructor injection (shown above) vs something
   like `boost::di`. Recommendation: raw constructor injection; ETIL
   subsystem count is small enough that a DI container is overkill.
5. **Static vs shared library.** Static for now; shared only if the TUI or
   external tools need plugin-style loading.

---

## 15. RBAC for channels

Channel operations are security-sensitive: read access leaks data,
write access can spam or forge events, route admin can redirect data
off-box. Every interaction must be gated by per-role permissions.

This section integrates with the existing role model in
`include/etil/mcp/role_permissions.hpp`, which already carries six
resource domains (System, LVFS, Network Client, Network Server, Code
Execution, Database). **Channels becomes the seventh domain.**

### 15.1 Principles

1. **Default deny.** An empty permission set grants no channel access,
   neither read nor write.
2. **Primary on/off per role.** A top-level `channels_enabled` boolean
   is the coarse master switch. When `false`, **no** channel operation
   succeeds regardless of finer grants — even reads on permissive
   channels fail. This is the auditable kill-switch for a role.
3. **Fine-grained grants per (pattern, action) when the master is on.**
   Individual channel patterns get per-action grants
   (Read, Write, Route, Introspect).
4. **Hierarchical patterns.** Dotted channel names plus `*` / `**`
   wildcards. `etil.evolution.*` matches one segment; `etil.evolution.**`
   matches any depth.
5. **Most-specific wins.** Longer literal prefix beats shorter. Explicit
   Deny at any level overrides Allow.
6. **Hard-wired channels bypass grants** for their designated action
   (see §15.5). Producers of audit/security/bootstrap events must not be
   silenceable by role configuration.
7. **Standalone mode preserved.** When `RolePermissions*` is `nullptr`
   (existing standalone-mode convention), channel access is unrestricted
   — matches the struct's documented semantics in its header comment.
8. **Every denial is audited.** Denied operations emit on
   `etil.aaa.audit.channel.denied` (itself hard-wired writable) with the
   role, channel, action, and session for post-mortem.

### 15.2 Schema — extend `RolePermissions`

Add a seventh domain block to `RolePermissions`:

```cpp
// --- Channels (I/O pipeline) ---
// See 20260418B-IO-Channel-Pipeline-Architecture.md §15
bool channels_enabled = false;              // primary on/off — master switch
std::vector<ChannelGrant> channel_grants;   // per-pattern fine grants
int channel_publish_quota = 1000;           // messages published per session
int channel_subscribe_quota = 10;           // concurrent subscriptions per session
bool channels_route_admin = false;          // add/remove routes (dangerous)
bool channels_network_sink = false;         // attach udp/tcp sinks (very dangerous)
```

And a new supporting struct:

```cpp
struct ChannelGrant {
    std::string pattern;                    // e.g. "etil.evolution.**"
    uint8_t actions = 0;                    // bitmask
    enum class Effect : uint8_t { Allow = 0, Deny = 1 };
    Effect effect = Effect::Allow;
};

enum class ChannelAction : uint8_t {
    None       = 0,
    Read       = 1 << 0,  // subscribe / observe
    Write      = 1 << 1,  // publish
    Route      = 1 << 2,  // install transforms or sinks on this channel
    Introspect = 1 << 3,  // list channels / list routes / query schema
};
```

### 15.3 Decision procedure

Given a request `(role, channel, action)`:

1. If `role == nullptr` (standalone mode) → **Allow**.
2. If channel matches a hard-wired entry for this action (§15.5) →
   **Allow**, bypass grants.
3. If `role->channels_enabled == false` → **Deny** (audit).
4. If `action == Route` and `!role->channels_route_admin` → **Deny**.
5. Walk `role->channel_grants` collecting all entries whose `pattern`
   matches `channel`. Sort by specificity (longer literal prefix first),
   with Deny ranking above Allow at equal specificity.
6. Take the first matching entry. If it has the requested `action` bit,
   and `effect == Allow` → **Allow**. Otherwise → **Deny**.
7. If no entry matches → **Deny** (default deny).
8. On **Allow**, check per-session quotas (`publish_quota`,
   `subscribe_quota`). Quota exhausted → **Deny** (rate-limit audit).
9. On any **Deny**, publish an `etil.aaa.audit.channel.denied` message
   with `{role, channel, action, reason}`.

This is evaluated inside `ChannelService::publish()`,
`ChannelService::observe()`, and `ChannelService::add_route()` before
the operation proceeds.

### 15.4 Principal binding

Every `ExecutionContext` carries a principal (role + user_id + session_id)
obtained from the authenticated session at creation. The
`ChannelService` requires a principal on every call:

```cpp
service->publish(principal, msg);
service->observe(principal, "etil.evolution.**");
service->add_route(principal, route_spec);
```

For in-process system code operating outside any session (the bootstrap
logger, the MCP server accepting a new connection, the evolution engine
on its own thread), a special `Principal::system()` value is used. The
system principal bypasses permission checks — it is the mechanism the
hard-wired channels rely on.

TIL words look up the current `ExecutionContext`'s principal implicitly,
so user-level code never forges a principal. Unit tests construct a
`TestPrincipal` with explicit grants to exercise edge cases.

### 15.5 Hard-wired channels

Some channels must always be writable or readable regardless of role
configuration:

- **`etil.system.bootstrap.**`** — Write + Read hard-wired. System writes;
  admin role reads. Startup/shutdown events must flow even when no role
  context exists.
- **`etil.aaa.audit.**`** — Write hard-wired. System writes; never
  user-writable. A role must not be able to forge audit entries.
- **`etil.aaa.audit.channel.denied`** — Read hard-wired for admin role.
  Correlation target for Falco alerts on permission-denial bursts.
- **`etil.security.**`** — Write hard-wired. System writes; admin-only
  reads. Falco-style alerts and AAA failures cannot be suppressed by a
  misconfigured role.
- **`etil.health.**`** — Write hard-wired. System writes; readable by any
  authenticated role. Liveness/heartbeat must flow to any monitor.
- **`etil.logging.error`** — Write hard-wired. Any code may raise a log
  error — a safety valve so a broken subsystem can report its own
  failure even when the rest of its channel access is revoked.

**Rationale**:

- **Audit write-hard-wired**: a role must not be able to silence its own
  audit trail. This would defeat the auditing purpose entirely.
- **Security write-hard-wired**: Falco-style alerts and AAA failures
  cannot be suppressed by a misconfigured role.
- **Bootstrap write-hard-wired**: early startup has no role context yet.
- **Health write-hard-wired**: heartbeat / liveness channels must flow
  even when the authenticated role is restrictive.
- **Admin reads**: ensure that operators retain visibility into
  security-sensitive streams regardless of their grants.

Hard-wired channels are declared in one place — a static
`kHardwiredChannels` table in the channel library — and the permission
decision procedure consults it before consulting role grants.

**Delivery guarantee.** The hard-wired write subset
(`etil.aaa.audit.**`, `etil.security.**`, `etil.system.bootstrap.**`,
`etil.logging.error`) also bypasses the ring-buffered delivery path
and delivers **inline on the producer thread** — see §22.2. Because
the no-block decision (§22 Answer A1) prohibits producer-thread
stalls, the combination of "must-emit" + "must-not-block" means the
only viable transport for these channels is direct synchronous
delivery to a fast sink (typically a file sink). This protects audit
and security channels from buffer-overflow drops that would otherwise
defeat their purpose.

### 15.6 Example role configurations

```cpp
// "operator" — monitors MCP, reads evolution diagnostics, cannot publish
RolePermissions operator_role = {
    .channels_enabled = true,
    .channel_grants = {
        {"etil.mcp.**",         Read | Introspect, Allow},
        {"etil.evolution.**",   Read | Introspect, Allow},
        {"etil.aaa.audit.**",   Read,              Allow},  // admin granted explicitly
    },
    .channel_publish_quota = 0,             // no writes
    .channel_subscribe_quota = 20,
};

// "evolution-researcher" — reads and writes evolution, nothing else
RolePermissions researcher_role = {
    .channels_enabled = true,
    .channel_grants = {
        {"etil.evolution.**",   Read | Write | Introspect, Allow},
        {"etil.repl.**",        Read | Write,              Allow},
    },
    .channel_publish_quota = 10000,
    .channel_subscribe_quota = 10,
};

// "guest" — channels master-switched off; no operations succeed
RolePermissions guest_role = {
    .channels_enabled = false,              // <-- primary on/off
    .channel_grants = {},
};

// "admin" — everything, including route admin and network sinks
RolePermissions admin_role = {
    .channels_enabled       = true,
    .channels_route_admin   = true,
    .channels_network_sink  = true,
    .channel_grants = {
        {"**",                  Read | Write | Route | Introspect, Allow},
    },
    .channel_publish_quota   = 100000,
    .channel_subscribe_quota = 100,
};
```

### 15.7 Network sinks — additional gate

Attaching `udp_sink` or `tcp_sink` requires **both**:

1. `channels_route_admin == true` (may add routes at all), **and**
2. `channels_network_sink == true` (may add routes whose sink sends
   data off-box).

This double-gate enforces the security rules in
`20260214A-ETIL-Server-Security-Rules.md`: data exfiltration via
network sinks is a privileged operation distinct from ordinary routing.
The role model reflects that distinction.

Further, every network sink's destination host/port is matched against
an allowlist configured in the channel service (`net_client_domains`
style) before the sink is permitted to attach. A role with
`channels_network_sink == true` can still only target approved
collectors.

### 15.8 TIL control surface — permission-aware words

Role-admin words (require `role_admin == true`):

```
role-grant-channel    ( actions pattern role-name -- )
role-revoke-channel   ( pattern role-name -- )
role-channel-enable!  ( bool role-name -- )         # the master switch
role-network-sink!    ( bool role-name -- )
```

Self-introspection words (allowed for any authenticated role):

```
channel-perm-check    ( action channel-name -- bool )  # am I allowed?
channel-perm-list     ( -- array )                     # my grants
channel-list          ( -- array )                     # channels I can introspect
```

### 15.9 Audit records

The `etil.aaa.audit.channel.*` namespace captures all permission
decisions. Minimum fields per audit record:

- `role` — role name at time of decision
- `user_id` / `session_id` — principal identity
- `channel` — channel operated on
- `action` — Read / Write / Route / Introspect
- `effect` — Allow / Deny
- `reason` — "master-off", "no-matching-grant", "quota-exhausted",
  "hard-wired-bypass", "explicit-deny", "allowed-by-grant-{pattern}"
- `timestamp`

Audit events go to two sinks in parallel: a `file_sink` for durable
record and the Mongo `audit_log` collection (via the existing AAA
infrastructure). Denial records are also forwarded to
`etil.security.access-denied` for Falco-style alerting on repeated
denials from the same principal.

### 15.10 Testing

Unit tests for permission resolution:

- Master switch off → all operations denied regardless of grants.
- Hard-wired channel write → allowed for system principal, allowed for
  any role (writes are never gated on hard-wired-write channels).
- Hard-wired channel read → allowed for intended reader role, denied
  for other roles.
- Specificity ranking: `etil.evolution.** = Allow` +
  `etil.evolution.fitness = Deny` → fitness-specific deny wins.
- Quota exhaustion → subsequent publishes denied, audit records emitted.
- Network sink without `channels_network_sink` → route-add denied even
  when `channels_route_admin == true`.

Property-based test: generate random role configurations + random
channel operations; verify the decision procedure is deterministic,
monotonic (adding Allow grants never turns Allow into Deny), and that
hard-wired channels are never silenceable.

---

## 16. Message broker sinks — offload fanout to a message server

### 16.1 Motivation

The in-process routing model (§3–§7) is fine when ETIL has a handful of
local sinks: a file, a UDP socket, an observable. It does not scale when
there are many interested consumers — TUI instances, remote archivers,
metrics collectors, replay harnesses, third-party tooling — each wanting
the same stream of events.

Fanning out to N subscribers from inside the ETIL process means:

- N TCP connections maintained by the MCP server, competing with its
  normal request/response traffic.
- O(N) per-publish work copying each message to N sinks.
- Subscriber lifecycle (connect / disconnect / backpressure) burning
  cycles that should be spent interpreting.
- No cross-process replay, no durability, no multi-tenancy.

A **message broker as a terminal sink** solves all four. ETIL writes to
one broker connection; the broker handles fanout, durability,
backpressure, and subscriber lifecycle. The interpreter never sees N.

```
ETIL ChannelService.publish(msg)
  └─ broker_sink  (one TCP/TLS connection)
        └─ Broker
             ├─ TUI subscriber (AMQP / NATS / Kafka consumer)
             ├─ File archiver
             ├─ Metrics collector
             ├─ Replay recorder
             └─ ...
```

The interpreter's per-publish cost is O(1): serialize + send once. The
broker absorbs the multicast.

### 16.2 Why not Redis

Redis Streams was historically a quick answer for this pattern. We
reject it on security grounds:

- **Authentication**: single `AUTH` password up to Redis 6, ACLs added
  only in 6.0 and remain broad. No per-stream authZ matching our
  channel RBAC in §15.
- **Encryption**: TLS supported but not default; many deployments run
  plaintext inside a "trusted" network that is not actually trusted.
- **Authorization model**: ACLs are command-pattern based, not
  topic/subject based. Maps awkwardly to dotted channel hierarchies.
- **Durability semantics**: Redis Streams require careful AOF tuning to
  avoid data loss on crash; not a natural fit for audit streams.

The channel library treats Redis as **not recommended**. A `redis_sink`
may be added for legacy interop but will not be a supported production
target.

### 16.3 Apache broker candidates

Researched 2025–2026 status, footprint, C++ client maturity, security
features, and topic-model fit with our dotted channel names.

**Apache ActiveMQ Artemis + Qpid Proton C++ — recommended Apache option.**
Artemis 2.40.x is the designated successor to classic ActiveMQ and is
actively released. Qpid Proton-C++ (AMQP 1.0 reactor client) is the
canonical C++ library — no JVM on the client side, event-driven API,
stable. Security: TLS, SASL (PLAIN/SCRAM/EXTERNAL/ANONYMOUS), file-or-LDAP
ACLs per address with `createDurableQueue` / `send` / `consume` verbs.
Addresses are natively dotted with `*` (one segment) and `#` (any depth)
wildcards — `etil.evolution.fitness` and `etil.evolution.#` work
identically to our channel grammar. Broker is JVM-hosted, realistic floor
350–500 MB RSS with a tuned 256 MB heap — workable on small-footprint
ARM deployments (1 GB RAM class) but consumes half the box. Best
Apache-branded fit.

**Apache Kafka + librdkafka — unsuitable for small-footprint hosts, viable at scale.**
Kafka 3.9/4.0 (KRaft, ZooKeeper retired) is extremely healthy; librdkafka
is the gold-standard C/C++ client. Security is strong (TLS, SASL
including OAUTHBEARER, per-topic/consumer-group/transactional-id ACLs).
Two disqualifiers for us: the broker wants 1–2 GB heap plus page cache
(too heavy for small-footprint targets), and topic names are flat — dotted hierarchy is
convention only, no broker-side wildcard subscription. Consumers match
topics via client-side regex, which defeats the routing model. Good
choice only if/when we have a dedicated broker host and need streaming-
platform features (replay, partitioning, consumer groups). Not Phase 1.

**Apache Pulsar + pulsar-client-cpp — feature-rich, too heavy for us.**
Pulsar 3.3 LTS / 4.0, `pulsar-client-cpp` modern and async-friendly.
Finest-grained security of the four (TLS/mTLS, JWT, OAuth2, Athenz,
Kerberos; tenant/namespace/topic ACLs). Three-component stack
(broker + BookKeeper + ZooKeeper-or-ZK-less) realistically 1.5–2 GB.
Topics are `persistent://tenant/namespace/topic` — dotted names map
awkwardly to namespaces. Good if we ever need multi-tenancy + geo
replication; overkill for the ETIL use case on a single box.

**Apache RocketMQ — rejected: dead C++ client.**
`rocketmq-client-cpp` last meaningful release 2020, the newer gRPC-based
client family has no C++ target. Not a viable path for a new C++
integration in 2026.

### 16.4 Honest answer: NATS may fit better

The user asked specifically about Apache, and Artemis is the right
Apache-Software-Foundation answer. But the constraint set
(dotted-hierarchy subjects, 10s-of-MB footprint target, strong
per-subject authZ) is answered almost exactly by **NATS with JetStream**,
which is a CNCF project — not ASF, but foundation-governed under
Apache 2.0 on comparable governance terms.

- `nats.c` client: actively maintained, Apache 2.0 licensed, clean C
  API, trivially wrapped in C++20. Last release 2025.
- Subjects are natively dotted with `*` and `>` wildcards — our channel
  grammar maps 1:1.
- Security: TLS, NKeys, JWT with decentralized auth model, per-subject
  allow/deny ACLs in accounts. Stronger than Redis, comparable to
  Artemis ACLs.
- Footprint: `nats-server` idles at 10–20 MB RSS on ARM. JetStream
  (durable streams) ships in-tree in `nats-server` and adds modest
  overhead.
- **License and governance (verified 2026-04-18):** `nats-server`
  (including JetStream) and `nats.c` are **Apache 2.0**. The project is
  hosted at CNCF with NATS trademarks assigned to the Linux Foundation.
  In April–May 2025 Synadia, the original creators, attempted to
  withdraw NATS from CNCF and relicense the server under the Business
  Source License (BUSL). CNCF and Synadia settled in May 2025: code
  and infrastructure stay at CNCF under Apache 2.0, trademarks moved
  to the Linux Foundation, project remains foundation-governed rather
  than single-vendor. That resolution materially strengthens NATS as a
  governance-safe dependency relative to the position before the
  dispute.

For small-footprint ARM deployment, NATS is the clean answer. If policy requires
Apache Software Foundation governance specifically (as distinct from
Apache 2.0 licensing under any foundation), Artemis is the answer and
we accept the footprint. In practice the choice is between two
foundation-governed Apache-2.0-licensed projects; the differentiator
is footprint + subject-model fit vs which foundation.

### 16.5 Integration shape

New sink family in `libetil-manifold`:

- **`amqp_sink`** → Artemis (or any AMQP 1.0 broker) via Qpid Proton-C++.
  Primary Apache-branded target.
- **`nats_sink`** → NATS / JetStream via nats.c. Primary pragmatic target
  (footprint + subject hierarchy fit).
- **`kafka_sink`** → Kafka via librdkafka. Phase N, if/when we need
  streaming-platform features or dedicated broker hosts.
- **`pulsar_sink`** → Pulsar via pulsar-client-cpp. Phase N, unlikely
  near-term; only if multi-tenancy or geo-replication becomes a need.

Each sink:

- Takes a connection config (broker URL, credentials, TLS cert path).
- Maps channel names to broker topics/subjects (usually identity — our
  dotted names work natively on Artemis and NATS).
- Serializes `Message.payload` via one of the existing transforms
  (`json_encoder`, `formatter`, or a binary codec for typed payloads).
- Respects message durability policy per channel (audit channels use
  durable/persistent mode; trace channels use best-effort).
- Reports broker-side failures back onto `etil.logging.error` (which is
  hard-wired writable — §15.5).

The broker connection is a **sink**, not a router replacement. In-process
routes (file, spdlog, observable) continue to coexist; the broker sink
is simply one more terminal consumer.

### 16.6 Interaction with RBAC (§15)

Attaching a broker sink to a channel is a privileged operation and must
pass the same network-sink gate:

- `channels_route_admin == true` (may add routes).
- `channels_network_sink == true` (may send data off-box).
- Broker destination (host:port) matches the channel service's broker
  allowlist (configured at process start, not role-settable).
- Broker-side credentials used by the sink are scoped per principal:
  the sink uses a credential bound to the role of whoever attached it,
  so broker ACLs can enforce authZ even if ETIL's own permission
  check is bypassed or misconfigured. Defense in depth.

A compromised ETIL role cannot exfiltrate data by attaching an
`amqp_sink` to `etil.aaa.audit.**` because (a) their broker credentials
lack permission to write to the audit-destination topic, and (b) the
allowlist rejects unknown destinations.

### 16.7 Deployment modes

**Mode A — embedded / co-located broker.** Broker runs on the same
host as ETIL (appropriate for self-hosted single-user deployments, or
development). Broker listens on loopback; ETIL connects locally.
`nats-server` or a tuned Artemis instance with 256 MB heap.

**Mode B — shared broker (LAN).** Broker on a dedicated host; multiple
ETIL instances plus subscribers connect. TLS mandatory, SASL/mTLS
authN, per-topic ACLs. The broker becomes the pub/sub backbone.

**Mode C — no broker.** Channel service uses only in-process sinks
(file, spdlog, observable). Default mode for CI, unit tests, and any
deployment that does not need cross-process fanout. No broker
dependency at build or run time — `amqp_sink` / `nats_sink` are
optional CMake features (`ETIL_BUILD_AMQP_SINK`, `ETIL_BUILD_NATS_SINK`),
off by default. Matches the existing optional-feature pattern for
`ETIL_BUILD_MONGODB` / `ETIL_BUILD_HTTP_CLIENT`.

### 16.8 Phasing

Broker sinks are **Phase 3 or later** in the rollout (§12):

1. Phase 0: logging substrate (no broker dependency).
2. Phase 1: channel library core + RBAC.
3. Phase 2: observable bridge, TIL words.
4. Phase 3: network sinks — add `nats_sink` first (smallest
   integration surface, closest fit), `amqp_sink` next if Apache
   governance becomes a requirement.
5. Phase 4+: source migration, `kafka_sink` / `pulsar_sink` only if
   concrete use cases emerge.

No broker dependency lands before Phase 3. Phases 0–2 are self-contained
and deliverable without committing to a broker choice.

### 16.9 License compatibility with ETIL (BSD-3-Clause)

All four broker client libraries are Apache 2.0:

- `nats.c` — Apache 2.0
- Qpid Proton-C++ — Apache 2.0 (Apache Qpid project)
- `librdkafka` — BSD-2-Clause (not Apache, but also permissive)
- `pulsar-client-cpp` — Apache 2.0

All four brokers are Apache 2.0 (NATS + JetStream, Artemis, Kafka,
Pulsar). This is the same license that already covers **Abseil** and
**Google Benchmark** in the ETIL dependency graph today, so there is
no new license-category risk in adding any of them.

**ETIL's BSD-3-Clause source is unaffected.** Linking against an
Apache-2.0 library does not relicense ETIL. Every ETIL source file keeps
its existing `SPDX-License-Identifier: BSD-3-Clause` header. Apache 2.0
is a permissive license and does not carry a "viral" relicensing clause
(that's the GPL).

**Obligations when distributing binaries** that include a broker client
statically or bundle `libX.so`:

1. Ship the Apache 2.0 license text for the library (typical layout:
   `third-party/LICENSE-nats.txt`, or a combined `NOTICES` file).
2. Preserve the upstream `NOTICE` file if the library has one.
3. Retain copyright notices (the toolchain embeds these in object files
   automatically).
4. If ETIL forks and modifies the library, disclose the modifications
   per Apache 2.0 §4(b). Not applicable when consuming via unmodified
   FetchContent or a distro package.

**No obligations for runtime-only dependencies.** When ETIL connects to
a separately-running `nats-server` (or Artemis broker) over the wire,
that is protocol-level use, not distribution. ETIL's license posture
is unchanged, same as when it talks to MongoDB or PostgreSQL. The
broker's own image/package carries its own notices; ETIL is not a
redistributor.

**Patent grant asymmetry (informational).** Apache 2.0 §3 includes an
explicit patent grant with a defensive termination clause: downstream
users of Apache-2.0 code who file a patent suit against that code
lose the patent grant. BSD-3 has no patent language. Practical effect:
when ETIL ships binaries containing an Apache-2.0 broker client,
downstream users get Apache's patent grant covering the bundled
library. This **does not alter ETIL's own patent posture** on its BSD-3
code — Apache's grant applies only to the Apache-licensed portion.

**Summary**: adding `nats.c` (or any of the four broker clients) is
license-equivalent to the existing Abseil dependency. No action beyond
standard NOTICE-file hygiene during binary distribution.

### 16.10 Decision deferred

The Artemis-vs-NATS choice is a **Phase 3 decision**. Phases 0–2 do not
depend on it. When Phase 3 opens:

- If Apache-governance is required for licensing/policy reasons → Artemis.
- If footprint + hierarchical-subject fit + operational simplicity
  dominate → NATS + JetStream.
- The `amqp_sink` and `nats_sink` are independent CMake features; both
  can ship in parallel and operators pick per deployment.

---

## 17. MCP SSE as a first-class channel endpoint

The MCP transport carries **two** SSE streams conceptually, and both
need to become channel endpoints rather than ad-hoc callbacks:

- **Outbound (server → client)** — already implemented today via the
  `sys-notification` and `user-notification` TIL words. Production
  path goes through `ExecutionContext::notification_sender_` /
  `targeted_notification_sender_` callbacks wired up in
  `src/mcp/tool_handlers.cpp`, buffered per-request-thread in
  `HttpTransport::pending_notifications_`, and flushed as
  `text/event-stream` chunked content. Good primitives, ad-hoc wiring.
- **Inbound (client → server via GET /mcp)** — explicitly **not yet
  supported**. `http_transport.cpp:189, 368` returns
  `"Method Not Allowed: SSE not yet supported"`. No TIL words exist
  for receiving client-initiated notifications or progress events.

Both sides move into the channel pipeline. The user-facing TIL words
stay; the wiring underneath becomes channel-based.

### 17.1 Current state (cited)

- `src/core/primitives.cpp:2867` `prim_sys_notification` — checks
  `permissions_->send_system_notification`, pops a string, calls
  `ctx.notification_sender()` when set.
- `src/core/primitives.cpp:2945` `prim_user_notification` —
  `permissions_->send_user_notification` check, calls
  `ctx.targeted_notification_sender()`.
- `include/etil/core/execution_context.hpp:441, 447`
  `NotificationSender` / `TargetedNotificationSender` std::function
  callback types.
- `include/etil/core/execution_context.hpp:433, 526`
  `queue_notification` / `drain_notifications` / `notifications_`
  buffered vector (per-context).
- `src/mcp/tool_handlers.cpp:513, 518, 539`
  `ctx.set_notification_sender` and `set_targeted_notification_sender`
  on each `interpret` invocation; cleared when the call ends.
- `src/mcp/http_transport.cpp:23, 27`
  `pending_notifications_` thread-local buffer and `notification_sink_`
  streaming writer. Two modes: collected-and-flushed vs streamed.

### 17.2 Outbound (server → client) — channel-based

Producers publish; a per-session sink delivers SSE:

**Channels:**

- `etil.mcp.out.notification.system` — any session-wide message
- `etil.mcp.out.notification.user` — message targeted at a specific user
- `etil.mcp.out.progress` — MCP `notifications/progress` events
- `etil.mcp.out.log` — MCP `notifications/message` events for structured
  logs visible to the client
- `etil.mcp.out.resource.updated` — MCP resource subscription updates
- `etil.mcp.out.tools.list_changed` — tool-inventory change notification

**Session scoping via tags, not channel names.** Every outbound message
carries tags:

```
tags = {
  session_id: "sess-abc123",
  user_id:    "user-42",            // for user-notification
  target_user_id: "user-99",        // for user-notification targets
  role:       "evolution-researcher",
}
```

A `mcp_sse_out_sink` is attached per session with a filter
`session_id == <this session>` and writes the SSE stream for that
session's HTTP response. One sink instance per open MCP stream;
sinks detach when the stream closes.

**The existing `NotificationSender` callbacks become thin adapters.**
`ctx.set_notification_sender` wraps a closure that calls
`channels->publish(Message{channel = "etil.mcp.out.notification.system",
tags = {session_id, user_id, role}, payload = msg})`. The
thread-local buffered vs streaming modes in `http_transport.cpp`
become properties of the sink (buffering transform optional; default
pass-through to the SSE writer).

**`sys-notification` and `user-notification` TIL words are unchanged.**
Their current signatures, permission checks, and behaviors stay. The
transport underneath swaps from `std::function` callback to channel
publish; TIL code that uses these words keeps working with zero
modification.

### 17.3 Inbound (client → server) — NEW observable API

This is the gap. MCP clients can send `notifications/progress`,
`notifications/cancelled`, `notifications/roots/list_changed`, and
arbitrary custom notifications. Today ETIL has no API to receive them.

**Channels (new):**

- `etil.mcp.in.notification.*` — generic client notifications
  (matched by the `method` field of the JSON-RPC envelope)
- `etil.mcp.in.progress` — `notifications/progress`
- `etil.mcp.in.cancelled` — `notifications/cancelled`
- `etil.mcp.in.roots.changed` — `notifications/roots/list_changed`
- `etil.mcp.in.request.*` — client-initiated requests (sampling,
  `roots/list`, etc.) routed as channel messages when the server
  supports them

The `mcp_sse_in_source` adapter sits on the HTTP layer: when a GET /mcp
SSE stream is open (once §17.7 implements it) or a client POST /mcp
notification arrives, the adapter publishes onto the matching inbound
channel with `session_id` / `user_id` tags.

**TIL words (new) — async observable subscription:**

```
mcp-on-notification  ( method-pattern -- observable )
mcp-on-progress      ( -- observable )
mcp-on-cancelled     ( -- observable )
mcp-on-roots-changed ( -- observable )
mcp-on-request       ( method-pattern -- observable )
```

Each returns a `HeapObservable` that emits a message each time a
matching inbound event arrives. These are thin convenience wrappers
over the generic `channel-subscribe` word proposed in §7. They exist
because they are common enough to deserve named entry points and
they enforce the correct tag/session filter automatically.

TIL usage sketch:

```
: on-progress ( progress-msg -- )
  ." progress: " . cr ;

mcp-on-progress ' on-progress observable-subscribe
```

**Threading model.** Inbound events arrive on libuv I/O threads.
Running TIL on an I/O thread would block the event loop. The adapter
queues events onto a per-session ready queue that the session's
interpreter drains at cooperative yield points in the outer interpreter
— same pattern used by ETIL's existing observable primitives in
`src/core/observable_execution.cpp`. TIL handlers run on the interpreter
thread, not the I/O thread.

Subscription lifecycle ties to the `ExecutionContext`: when the context
is destroyed (session closes), its subscriptions are released and the
corresponding route handles removed from the channel service.

### 17.4 RBAC integration — preserve existing bools, add new ones

The existing `RolePermissions` fields stay authoritative for
notification gating. Channel grants on the `etil.mcp.out.**` /
`etil.mcp.in.**` subtrees are **additive** — both checks must pass.
This prevents an overly-broad `**` wildcard grant from implicitly
opening notification send/receive access that the legacy bool was
supposed to protect.

**Existing fields (unchanged semantics):**

- `send_system_notification` — authoritative for Write access to
  `etil.mcp.out.notification.system` and `etil.mcp.out.log`.
- `send_user_notification` — authoritative for Write access to
  `etil.mcp.out.notification.user`.

**New fields proposed for `RolePermissions`:**

```cpp
// --- MCP SSE (inbound observation) ---
bool receive_client_notification = false;   // subscribe etil.mcp.in.notification.**
bool receive_progress            = false;   // subscribe etil.mcp.in.progress
bool receive_cancelled           = true;    // default true — see §17.5
bool receive_roots_changed       = false;
int  mcp_subscribe_quota         = 10;      // concurrent inbound subscriptions
```

The decision procedure in §15.3 gains a pre-check: for channels under
`etil.mcp.out.notification.**` and `etil.mcp.in.**`, the corresponding
legacy bool must be `true` in addition to the channel grant resolving
to Allow. Both gates must pass.

### 17.5 Hard-wired additions to §15.5

Two additions to the hard-wired channel list:

- **`etil.mcp.in.cancelled`** — Read hard-wired for the owning session.
  A role cannot suppress reception of cancellation notifications for
  its own session. This is a safety invariant: cancellation must always
  be honored. `receive_cancelled` defaults `true` and is treated as
  always-on via the hard-wired bypass even if a misconfiguration sets
  it false.
- **`etil.mcp.out.log`** is *not* hard-wired. A role can be legitimately
  forbidden from emitting structured client-visible logs. Audit of the
  denial still goes to `etil.aaa.audit.channel.denied` (which *is*
  hard-wired writable, so the denial is recorded even though the
  original action failed).

### 17.6 Multiplexing via the broker sinks

Outbound notifications fanned out through an external broker (§16)
become powerful: instead of ETIL maintaining one SSE stream per MCP
client, ETIL publishes to the broker and clients subscribe there.
Works for scenarios where the "client" is actually a fleet (dashboards,
monitors) rather than a single TUI session. Inbound similarly: a
client notification arriving via AMQP/NATS is published onto
`etil.mcp.in.**` just like a native SSE arrival. The ETIL interpreter
does not know or care which transport delivered the message.

### 17.7 Migration sequence

1. **Phase A (Phase 1 of the library rollout)** — add the outbound
   channels as an in-process transport. `notification_sender_` becomes
   a closure that publishes; `mcp_sse_out_sink` attaches per session
   and reproduces today's SSE behavior. No TIL behavior change. The
   thread-local `pending_notifications_` buffer is owned by the sink.
2. **Phase B (Phase 2)** — implement GET /mcp SSE endpoint
   (the line 189 TODO). Route inbound client events onto
   `etil.mcp.in.**` channels. Expose `channel-subscribe` and
   introduce the `mcp-on-*` convenience words. Add the new
   `RolePermissions` fields.
3. **Phase C (Phase 3)** — add the `mcp_notification_sink` family
   backed by broker sinks (AMQP/NATS) so notifications fan out to
   subscribers outside the MCP stream entirely. Optional per
   deployment.

Backward compatibility throughout: `sys-notification`,
`user-notification`, `send_system_notification`,
`send_user_notification`, and the `NotificationSender` / targeted
callback types on `ExecutionContext` are preserved. The callbacks
become adapters; the primitives keep their signatures; the permission
bools remain authoritative.

### 17.8 Open questions specific to SSE

- **Backpressure on outbound.** Resolved in §22 Answer A1 / §22.2:
  ring-buffered per route, `drop_first` default (keep newest — matches
  what MCP clients typically want to see), `drop_last` optional,
  **never block**. Today's `MAX_PENDING_NOTIFICATIONS` cap in
  `http_transport.cpp` maps to the new ring buffer with drop-first
  overflow. Hard-wired channels (cancellation, audit) bypass the ring
  and deliver inline — see §22.2.
- **Reconnect semantics.** MCP clients may disconnect and reconnect.
  Does the new sink re-subscribe to the same session and replay
  missed messages? Only broker-backed paths can offer replay;
  in-process sinks cannot. Document this asymmetry.
- **Ordering guarantees.** Addressed by §18 — every message carries
  an origin tuple `(hostname, app_startup_us, session_id, seq)` with
  `seq` sourced from a process-global atomic counter. This gives
  total order within an origin, globally-unique identity across all
  origins, and deterministic tiebreak for display/indexing. See §18
  for the full scheme.

---

## 18. Message identity: origin tuple + sequence counter

Every `Message` carries a `MessageOrigin` tuple (see §3) that
identifies the exact event globally and unambiguously:

```
(hostname, app_startup_us, session_id, seq)
```

### 18.1 What each component does

- **`hostname`** — fully-qualified hostname of the machine producing
  the message. Disambiguates across a fleet.
- **`app_startup_us`** — UTC microseconds at `ChannelService::init()`.
  Disambiguates *process generations* on a single host. Without this,
  two successive runs of the same app on the same host would reuse
  sequence numbers starting from 0, and `(host, session, seq)` alone
  could collide across runs.
- **`session_id`** — MCP session identifier. Empty / `"system"`
  sentinel for messages produced outside any session (bootstrap,
  evolution running on the server's own thread, AAA audit from an
  unauthenticated path). Useful for filtering and routing; not
  strictly required for uniqueness since the seq is app-global.
- **`seq`** — strictly increasing `int64_t` from a **process-global
  atomic counter**. Every call to `ChannelService::publish()`
  performs `counter_.fetch_add(1, std::memory_order_relaxed)` and
  stamps the returned value on the message. Guaranteed unique and
  monotonic within an app instance.

### 18.2 Uniqueness, ordering, and identity

The tuple is the **authoritative message ID**. Properties:

1. **Globally unique.** Across all processes, hosts, and time,
   `(host, startup, session, seq)` identifies exactly one message.
2. **Total order within origin.** For two messages with the same
   `(host, startup)` — same process generation — `seq` gives an
   unambiguous happens-before ordering.
3. **Partial order across origins.** Messages from different origins
   are incomparable by causality, but a canonical lexicographic order
   on the tuple gives deterministic sort keys for display, indexing,
   and replay.
4. **Gap detection.** Subscribers can detect missed messages by
   watching for gaps in `seq` per origin.
5. **Safe deduplication.** A sink that receives the same message twice
   (crash-resend, broker redelivery) can drop it by tuple equality.

### 18.3 Cost

- **One relaxed atomic fetch_add per publish.** On x86 this is a
  single locked instruction; on ARM an LDADD. Uncontended. Cheaper
  than any other cost in the publish path.
- **8 bytes for `seq`, 8 bytes for `app_startup_us`.** Per-message
  overhead.
- **`hostname` stored once.** The `std::string_view` on each message
  references a process-global static string populated at init. Zero
  per-message allocation or copy for the hostname. Interned literal.
- **`session_id` is already in tags.** Promoting it to the origin
  struct is a direct copy — no additional lookup. Its presence in
  both origin and tags is deliberate: origin is identity, tags are
  routing and filtering, and keeping them separate simplifies sink
  logic that only cares about one or the other.

### 18.4 Capture at init

In `ChannelService::init()`:

```cpp
namespace etil::manifold {

static std::string hostname_storage_;          // process-global
static std::string_view hostname_;
static int64_t app_startup_us_ = 0;
static std::atomic<int64_t> seq_counter_{0};

void init(const Config& cfg) {
    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0) {
        buf[0] = '\0';
    }
    hostname_storage_.assign(buf);
    hostname_ = hostname_storage_;

    app_startup_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // Rest of init — sinks, transforms, routes, principal binding, RBAC.
}

MessageOrigin current_origin(const std::string& session_id) {
    return {
        .hostname       = hostname_,
        .app_startup_us = app_startup_us_,
        .session_id     = session_id,
        .seq            = seq_counter_.fetch_add(1, std::memory_order_relaxed),
    };
}

} // namespace etil::manifold
```

Every `publish()` call builds the `MessageOrigin` via
`current_origin(principal->session_id())` before dispatching.

### 18.5 Serialization at broker boundaries

When a message crosses the process boundary (AMQP / NATS / Kafka
sink), the tuple is serialized as the broker-level message ID plus
application headers:

- **NATS subject header** or Kafka record header `x-etil-origin`:
  `"{host}:{startup_us}:{session}:{seq}"`. One string, parseable.
- **JSON envelope** (when payload is JSON):
  ```json
  {
    "origin": {
      "host": "etil-host.example.com",
      "app_start_us": 1713443220123456,
      "session": "sess-abc123",
      "seq": 8472
    },
    "channel": "etil.mcp.out.notification.system",
    "ts_us": 1713443221998877,
    "tags": { "user_id": "user-42", "level": "info" },
    "payload": { "text": "evolution generation 47 complete" }
  }
  ```
- **Broker-native dedup keys**. NATS JetStream's `Nats-Msg-Id` and
  Kafka's transactional producer deduplication can use the origin
  tuple string directly — exactly-once delivery semantics come free.

### 18.6 Consumer use cases

- **Replay from a point.** A consumer crashes after processing
  `seq=5000` from origin X; on reconnect it subscribes with
  `since = (origin=X, seq=5000)` and receives 5001 onward. Broker-
  backed paths (§16) support this natively via JetStream /
  Kafka offsets. In-process paths cannot replay — note this
  asymmetry in §17.8.
- **Gap alerting.** An observer that sees seq jump from 4712 to 4715
  from origin X knows messages 4713–4714 were dropped or
  misdelivered. Emit an alert on `etil.security.gap-detected`.
- **Distributed trace stitching.** A message observed at an external
  collector can be tied back to the exact producer process via the
  origin tuple — invaluable for post-mortem of multi-host runs.
- **Forensic audit.** Given an audit log entry at time T, retrieve
  every message from the same origin in a surrounding window by
  filtering origin equality and a seq range.

### 18.7 Edge cases

- **Clock skew across hosts.** NTP drift between machines means
  `app_startup_us` from host A and host B are not directly comparable
  in wall-clock terms. This **does not affect identity uniqueness**:
  the tuple is unique regardless of clock skew because `hostname`
  distinguishes origins. Time-based *ordering* across hosts is a
  separate concern handled by logical clocks or broker-side timestamps,
  not by this identity scheme.
- **Two processes starting in the same microsecond on the same host.**
  Theoretically possible on a very fast system with simultaneous
  service restarts. If observed in practice, the origin can be
  extended with PID:
  `(hostname, app_startup_us, pid, session_id, seq)`. Not needed by
  default — PID is the extension path when it matters.
- **Counter overflow.** `int64_t` at 1M messages/second would take
  ~292,000 years to overflow. Not a concern.
- **Process restart preserving session.** If a client reconnects to
  the same session across an ETIL process restart, the new process
  has a fresh `app_startup_us` and `seq` restarts from 0. Same
  `session_id` but different origin generation — this is the *correct*
  behavior: the restart is a meaningful event in the message stream
  and consumers should see the origin change.

### 18.8 TIL introspection

The origin tuple is queryable from TIL for debugging:

```
channel-origin         ( -- host-str startup-us session-str )
channel-seq            ( -- seq )                # next seq value
channel-last-published ( -- msg-id-str )         # most recent publish on this session
```

Useful during benchmark analysis: a test can capture the origin before
a scenario, run the scenario, then scan the captured channel transcript
for exactly the seq range it produced.

### 18.9 Relationship to audit and security

The origin tuple is included in every `etil.aaa.audit.*` record and
every `etil.security.*` alert. This means any suspicious activity
detected downstream can be traced to the exact producer with no
ambiguity — critical for incident response. The audit record's
own origin tuple and the tuple of the message being audited are
both preserved, so you can reconstruct the full chain.

---

## 19. Manifold in etil-web (WASM / browser context)

The etil-web project (`workspace/etil-web/`) compiles the ETIL
interpreter to WASM via Emscripten and wraps it with xterm.js +
TypeScript for a browser-native REPL. Per `etil-web/CLAUDE.md`:
client-side only, no MCP server, IDBFS for persistence, GitHub Pages
deployment, ~5–7 MB gzipped binary. The WASM build already force-disables
several major features (see `CMakeLists.txt:45-51`: HTTP_CLIENT, JWT,
MONGODB, tests, examples). Manifold must fit the same operational
envelope.

### 19.1 What changes in the browser

- **No libuv.** All sinks and sources that rely on `uv_sink` /
  `uv_session` (TCP, UDP, raw sockets) are not available. Network I/O
  goes through `fetch()`, `WebSocket`, or `EventSource`.
- **No threads by default.** Single-threaded WASM unless the page is
  cross-origin-isolated (COOP/COEP headers) with SharedArrayBuffer —
  GitHub Pages does not serve those headers, so the default deployment
  is single-threaded. `std::atomic<int64_t>` still compiles and works;
  on a single-threaded target it degrades to plain load/store with
  zero overhead.
- **No `gethostname()`.** The WASM libc stub returns an empty string
  or a fixed placeholder. Manifold's `MessageOrigin::hostname` must
  be populated from a browser-appropriate source.
- **Time source precision clamped.** Browsers round
  `performance.now()` to ~5 µs (Chrome) or ~1 ms (Firefox/Safari in
  some modes) as a Spectre mitigation. `std::chrono::system_clock::now()`
  in Emscripten is backed by `Date.now()` plus `performance.now()` and
  inherits this clamping.
- **No file system except MEMFS / IDBFS / OPFS.** `file_sink` must
  target an Emscripten-mounted virtual FS. The etil-web build already
  uses IDBFS at `/home/web_user/` for persistence (per CLAUDE.md §
  "Persistence"); Manifold's durable file sinks write there.
- **No stderr / stdout in the POSIX sense.** Emscripten routes
  `stdout`/`stderr` writes to `console.log`/`console.error` by default.
  This actually *helps* — the stderr-ban policy from survey §11 is
  already automatic in the WASM build, and the console mapping becomes
  the right default sink (see §19.3).
- **No MCP server.** etil-web is a pure MCP *client*, if it connects to
  an MCP server at all. §17 inbound/outbound distinctions collapse:
  everything "inbound" for the native server becomes "outbound response
  from an external server" in the browser, and "outbound notification"
  becomes "inbound-from-server for the browser." Manifold channel names
  (`etil.mcp.in.*` / `etil.mcp.out.*`) are server-centric; the browser
  transport bridges them accordingly.

### 19.2 Browser-native sinks

Add these to §4's inventory, gated by `ETIL_WASM_TARGET` (or a
finer-grained `ETIL_MANIFOLD_BROWSER_SINKS` option):

- **`console_sink`** — maps message level to `console.debug` / `.info` /
  `.warn` / `.error`. The default sink in the WASM build, replacing
  `stderr_sink` for all bootstrap and error paths.
- **`devtools_sink`** — like `console_sink` but uses `console.group`,
  `console.time`, `console.trace` for structured entries; preserves
  payload as a JS object (via Emscripten `EM_ASM` marshaling) so
  DevTools can expand/inspect it. Source maps show the originating
  TIL or C++ location.
- **`notification_sink`** — Web Notifications API. Requires
  `Notification.permission === "granted"` (user prompt required on
  first use). Title/body/icon pulled from payload fields.
- **`broadcast_channel_sink`** — same-origin `BroadcastChannel` for
  cross-tab fanout. Useful when one tab runs the REPL and another
  is a monitor/inspector.
- **`postmessage_sink`** — `window.postMessage` to an iframe parent,
  child, or opener, with explicit `targetOrigin` allowlist (never `*`).
- **`custom_event_sink`** — `EventTarget.dispatchEvent(new CustomEvent(...))`
  on a well-known global target. Allows page-level JS subscribers
  (user scripts, host applications embedding etil-web) to observe
  Manifold channels via standard DOM event handlers.
- **`worker_sink`** — `postMessage` to a Web Worker. Offloads heavy
  subscribers (analyzers, archivers) off the main thread.
- **`indexeddb_sink`** — durable log storage in IndexedDB. Rotating
  schema with TTL. Durable across page reloads and browser restarts.
- **`opfs_sink`** — Origin Private File System where available
  (Chrome/Edge/Safari; not Firefox as of 2026-04). Higher throughput
  than IndexedDB for append-heavy loads; falls back to `indexeddb_sink`
  when OPFS is missing.
- **`websocket_sink`** — WebSocket to a remote broker or collector.
  Replaces `udp_sink` / `tcp_sink` for cross-process use. Respects
  Content-Security-Policy `connect-src` directives.
- **`fetch_sink`** — batched POST to an HTTP collector. Useful for
  low-rate telemetry; composes with `batcher` transform.
- **`eventsource_source`** (source, not sink) — subscribe to an
  external SSE stream and publish messages onto a channel. This is
  how etil-web receives MCP notifications from a remote MCP server.

### 19.3 Browser-native sources

Sources that publish onto `etil.web.**` channels:

- **`console_source`** — proxies `console.log/warn/error/debug/info`
  to capture page-wide console output onto
  `etil.web.console.{level}` channels. Useful for debugging etil-web
  itself and for surfacing third-party script output.
- **`window_error_source`** — `window.addEventListener('error', ...)`
  publishes onto `etil.web.error.uncaught`.
- **`unhandled_rejection_source`** — `'unhandledrejection'` → publishes
  onto `etil.web.promise.rejected`.
- **`postmessage_source`** — `window.onmessage` with origin allowlist;
  publishes accepted messages onto `etil.web.postmessage.in.*` tagged
  with `{web_origin, source_window_id}`. Rejects messages from
  non-allowlisted origins silently; rejected-attempt counts go to
  `etil.security.postmessage.rejected`.
- **`broadcast_channel_source`** — subscribe to a named BroadcastChannel
  and publish incoming messages onto `etil.web.broadcast.*`.
- **`performance_source`** — `PerformanceObserver` → publishes
  navigation timing, resource timing, long tasks onto
  `etil.web.perf.*` channels. Useful for benchmarking etil-web itself.
- **`service_worker_source`** (optional) — if a service worker is
  registered, publishes its lifecycle events onto `etil.web.sw.*`.
- **`devtools_protocol_source`** (exotic, optional) — if run under
  automation (Puppeteer, Playwright), Chrome DevTools Protocol events
  can bridge into Manifold. Development-time use only.

### 19.4 Channel naming for browser

Add an `etil.web.**` subtree to the namespace registry:

```
etil.web.console.{debug,info,warn,error}
etil.web.notification.{shown,clicked,closed}
etil.web.broadcast.in.<name>
etil.web.broadcast.out.<name>
etil.web.postmessage.in.<origin>
etil.web.postmessage.out
etil.web.dom.<event-type>            # optional; opt-in per element
etil.web.error.uncaught
etil.web.promise.rejected
etil.web.perf.{navigation,resource,longtask,paint}
etil.web.sw.{install,activate,fetch,message,error}
etil.web.storage.{quota,persisted}
etil.web.idb.{open,error}
```

### 19.5 `MessageOrigin` adapted for browser

The §18 identity tuple stays structurally identical but the field
sources change:

- **`hostname`** → `location.origin` at init (e.g.,
  `https://etil-org.github.io`). This is the **web origin**, not a DNS
  host — they happen to map onto the same field because Manifold's
  model treats "where did this come from" as the key identity axis.
  In cross-origin routing (§19.6) the web origin is also carried as
  a `web_origin` tag on incoming messages for routing decisions —
  see §19.7 for the naming collision note.
- **`app_startup_us`** → `Math.floor(performance.timeOrigin + performance.now()) * 1000`
  at init, clamped to microseconds. Given Spectre-mitigation clamping,
  realistic precision is ~5 µs (Chrome) to ~1 ms (Firefox). Uniqueness
  still holds: a page reload generates a new `timeOrigin`.
- **`session_id`** → the tab's per-instance UUID generated at page
  load (and persisted to `sessionStorage` for the tab lifetime), OR
  the MCP session ID if the REPL is connected to a remote MCP server.
  Empty string / `"local"` sentinel when the REPL is standalone.
- **`seq`** → `std::atomic<int64_t>` works fine. On single-threaded
  WASM it compiles to non-atomic ops; on multi-threaded (COOP/COEP)
  builds the atomic is genuine. Either way the guarantees hold.

### 19.6 Cross-origin routing

The browser's origin model is stricter than the native deployment's
process model; Manifold must enforce it explicitly.

- **Default deny on inbound postMessage.** `postmessage_source`
  requires an explicit allowlist of trusted web origins configured
  at init. Unknown senders are silently dropped and counted on
  `etil.security.postmessage.rejected`.
- **Explicit targetOrigin on outbound.** `postmessage_sink` rejects
  `targetOrigin === "*"` — every outbound postMessage must name the
  intended recipient origin. Forbids the common XSS footgun of
  leaking data to any parent frame.
- **WebSocket / fetch respect CSP.** The browser enforces
  Content-Security-Policy `connect-src` directives before Manifold
  can reach them. This is not a Manifold concern but operators must
  configure CSP to allow the intended collectors; a misconfigured
  CSP silently blocks the `websocket_sink` / `fetch_sink`.
- **BroadcastChannel is same-origin only.** Cannot be used for
  cross-origin routing by design. Manifold's `broadcast_channel_*`
  helpers are scoped accordingly.
- **Service Worker scope.** If a Service Worker is involved, its
  scope limits which requests it can intercept. Manifold does not
  rely on SW interception but may bridge SW messages via the
  `service_worker_source`.
- **CORS for outbound collectors.** An HTTP collector that accepts
  `fetch_sink` POSTs must respond with appropriate
  `Access-Control-Allow-Origin`. If the collector omits this,
  Manifold catches the browser-thrown network error and publishes
  on `etil.logging.error` (which is hard-wired writable).

### 19.7 Terminology: MessageOrigin vs web origin

The §18 `MessageOrigin` struct uses "origin" in the sense of
*provenance* — which producer process emitted this message. Browsers
use "origin" in the sense of *web security context* —
`scheme://host:port`. These are different concepts and must not be
conflated.

Convention going forward:

- **`MessageOrigin`** (struct) — the §18 identity tuple. Unchanged.
- **`web_origin`** (tag key on Message) — when a message arrived from
  or is destined for a cross-origin peer, the browser-security-context
  origin is carried in `tags["web_origin"]`. Used for RBAC trust
  decisions and postMessage routing.
- In prose: "origin tuple" for §18 identity; "web origin" for browser
  security context. Never just "origin" when context is ambiguous.

### 19.8 RBAC in the browser

The full `RolePermissions` model from §15 assumes JWT-authenticated
users and a persistent role store. etil-web has no JWT (CLAUDE.md
exclusion list: "no MCP server in browser"). Two practical modes:

- **Standalone mode.** `permissions == nullptr` per the
  `RolePermissions` documented convention. All Manifold operations
  permitted. This is the default for etil-web — the user IS the full
  principal, there is no other role to enforce against locally.
- **Connected mode.** etil-web connects to a remote MCP server.
  Operations that cross to the server (MCP requests, remote tool
  calls, broker publishes) are gated by the server's role enforcement;
  local browser operations remain standalone-permitted. The remote
  role's `receive_*` fields from §17 govern what the browser can
  subscribe to on the server side.

The cross-origin gate of §19.6 is a separate, always-enforced layer.
It is **not** part of `RolePermissions` — it is a browser-security
invariant that applies regardless of role, equivalent to the
hard-wired channels in §15.5.

### 19.9 Binary size and conditional compilation

etil-web is size-sensitive: ~5–7 MB gzipped for the existing build.
Manifold must not add significantly. Strategy:

- Manifold core (Message, Service, Sink interface, Router, in-process
  observable transport) is always linked. Target: <50 KB gzipped
  added to the WASM binary.
- Browser-specific sinks are gated by `ETIL_MANIFOLD_BROWSER_SINKS`
  (default ON in WASM build, OFF on native).
- Broker sinks (`amqp_sink`, `nats_sink`, `kafka_sink`, `pulsar_sink`)
  are gated by their respective `ETIL_MANIFOLD_*_SINK` options and
  are **force-disabled in WASM** in the same block as HTTP_CLIENT /
  JWT / MONGODB in `CMakeLists.txt:45-51`.
- spdlog is already a dependency on native; in WASM it either stays
  (builds fine under Emscripten) or is replaced by a
  `console_sink`-only minimal logging backend. Size measurement
  required before deciding.

Proposed addition to `CMakeLists.txt:45`:

```cmake
if(ETIL_WASM_TARGET)
    # ... existing disables ...
    set(ETIL_MANIFOLD_AMQP_SINK      OFF CACHE BOOL "" FORCE)
    set(ETIL_MANIFOLD_NATS_SINK      OFF CACHE BOOL "" FORCE)
    set(ETIL_MANIFOLD_KAFKA_SINK     OFF CACHE BOOL "" FORCE)
    set(ETIL_MANIFOLD_PULSAR_SINK    OFF CACHE BOOL "" FORCE)
    set(ETIL_MANIFOLD_BROWSER_SINKS  ON  CACHE BOOL "" FORCE)
endif()
```

### 19.10 TIL words specific to etil-web

Convenience wrappers over the generic `channel-*` vocabulary:

```
browser-notify       ( body-str title-str -- ok? )    # Web Notifications
browser-broadcast    ( msg-str name-str -- )          # BroadcastChannel
browser-postmessage  ( msg-str target-origin-str -- ) # window.postMessage
browser-storage-put  ( value-str key-str -- )         # IndexedDB or OPFS
browser-on-console   ( level-str -- observable )      # subscribe console
browser-on-error     ( -- observable )                # window.onerror
browser-on-message   ( -- observable )                # postMessage in
```

All of these publish to or subscribe from `etil.web.**` channels; they
exist only for ergonomics and go through standard permission gating.

### 19.11 etil-web deployment considerations

- **GitHub Pages origin** (`etil-org.github.io` or a custom domain)
  is the web origin. CORS policy on any external collector must
  allow this origin explicitly.
- **No cross-origin isolation by default.** SharedArrayBuffer
  unavailable; multi-threaded WASM not usable. Manifold's
  single-threaded path must be production-grade — which it is by
  design (atomic-on-single-thread is zero-cost).
- **Service Worker offline support.** If etil-web adds a SW for
  offline operation, Manifold's `indexeddb_sink` / `opfs_sink`
  continue to work offline. Broker sinks do not; they should fail
  gracefully to `indexeddb_sink` via a route fallback transform.
- **DevTools integration** via `devtools_sink` is the single most
  useful win for the "hard to follow what's going on" pain point
  that started this whole design — a structured channel dump into
  Chrome DevTools with expandable payload objects replaces the
  opaque "stdout wall of text" in the browser console.

### 19.12 Phasing for etil-web

etil-web adoption of Manifold follows the main rollout but adds:

- **Phase 1b** — alongside Manifold core, deliver `console_sink`,
  `window_error_source`, `unhandled_rejection_source`. These replace
  all existing ad-hoc console use in the TypeScript glue layer.
- **Phase 2b** — alongside observable bridge, add
  `broadcast_channel_*`, `postmessage_*`, `custom_event_sink`,
  `browser-*` TIL words.
- **Phase 3b** — alongside broker sinks, add `websocket_sink`,
  `fetch_sink`, `eventsource_source` for remote-collector and
  remote-MCP integration.
- **Phase 4b** — IDBFS / OPFS durable sinks; DevTools integration
  polish.

No etil-web phase depends on broker sinks (Phase 3 / 3b is optional
per deployment).

---

## 20. Cycle detection and routing safety

Manifold's multi-source / multi-sink topology makes feedback loops a
first-class concern rather than a curiosity. A handful of scenarios in
our own design create cycle risk by construction:

- **Broker bridges (§16).** ETIL `amqp_sink` publishes to a broker;
  an `amqp_source` on the *same broker* subscribes and republishes
  locally. The broker cannot tell its own publisher from an unrelated
  one without help from us.
- **Transform fan-out (§5).** A `fan_out` transform that targets the
  same channel it was installed on creates a tight local loop.
- **Observable round-trip.** A TIL word subscribes to a channel and
  (intentionally or not) publishes onto the same channel in its
  handler. Common user error.
- **Cross-tab BroadcastChannel (§19).** Tab A publishes; Tab B
  subscribes and mirrors to another channel that Tab A also
  subscribes to. Same-origin echo.
- **postMessage iframe chains.** Parent frame forwards to child,
  child forwards to parent on a different tag.
- **Audit-of-audit pathology.** If a cycle-detection audit writes to
  a channel that is itself subject to cycle detection and somehow
  triggers another audit, recursion follows.

Manifold enforces a **three-layer defense**: visited-trace cycle
detection (tight loops), hop-budget TTL (runaway cascades), and
origin echo suppression (broker bridges). All three are always on;
configuration chooses policy, not whether to enforce.

### 20.1 Layer 1 — Visited-channel trace

Every message carries `route_trace` — an ordered list of channels
it has passed through. `absl::InlinedVector<std::string, 4>` so
typical single-hop messages incur no heap allocation.

**Write rule.** Before the router dispatches a message to a sink or
a transform that republishes, it appends the target channel to
`route_trace`.

**Check rule.** Before dispatching, the router scans `route_trace`
for the target channel. If already present, the dispatch is
refused and the event is published on
`etil.aaa.audit.channel.cycle-detected` with the full trace and
current origin tuple.

**Cost.** `route_trace` is vector-of-string, bounded at 32 entries
(same as the hop cap — see §20.2). Worst-case O(32) linear scan per
publish; small-string optimization keeps most trace entries in
inline storage. Negligible compared to any sink's actual work.

### 20.2 Layer 2 — Hop budget (TTL)

`hops_remaining` defaults to 32 and decrements on every republish
(every transform that emits a new message, every source-adapter
that re-emits). Reaching zero drops the message and emits on
`etil.aaa.audit.channel.ttl-exhausted`.

**Why both trace and TTL.** They catch different pathologies:

- **Trace** catches the common case cleanly: channel revisit on
  any path back to the origin channel, same message identity.
- **TTL** catches amplification loops where each hop produces
  messages with *new* identities that elude the trace check —
  e.g., a transform that copies-and-republishes with a mutation
  that changes the identity just enough to appear unrelated.

The TTL is the belt-and-suspenders safety net. Hitting it is
**always** a bug (or attack) — the default 32 is a huge margin
over any legitimate topology.

### 20.3 Layer 3 — Origin echo suppression (broker-specific)

Broker sinks (§16) carry the full `MessageOrigin` tuple across the
wire (§18.5). The matching broker *sources* on the same broker
(when we grow them in Phase 4) inspect incoming messages:

- If `origin.hostname` and `origin.app_startup_us` match this
  process's own identity, the message is **our own echo** and is
  dropped (or forwarded with `echo=true` tag — configurable).
- Default policy: `reject_own_origin = true`. Explicit opt-out when
  a loop is intended (e.g., two ETIL processes A and B using a
  broker topic as shared state — each wants to see its own writes
  because the broker canonicalizes ordering).

**Separate from layers 1 and 2** because broker round-trips don't
preserve `route_trace` / `hops_remaining` across the wire. The
origin tuple is all we have, and it's sufficient.

### 20.4 Hard-wired channels are exempt from the check

Two hard-wired channels must be allowed to publish even when normal
cycle rules would block them:

- **`etil.aaa.audit.channel.cycle-detected`** — the cycle audit
  itself. If this publish were subject to the cycle check, a
  sufficiently perverse audit-sink configuration could itself
  cause cycles. Special-cased: audits always emit, regardless of
  `route_trace`.
- **`etil.aaa.audit.channel.ttl-exhausted`** — same reasoning, on
  the TTL path.

Both bypass both layers 1 and 2. Layer 3 (origin echo) still
applies because it is a broker-boundary concern.

Both also deliver **inline on the producer thread** per §15.5 /
§22.2 — they are descendants of `etil.aaa.audit.**`. This closes
the otherwise-open failure mode where a cycle storm overflows the
audit sink's ring buffer and drops the very records describing the
storm.

### 20.5 Transform semantics

Transforms fall into three categories for cycle-detection purposes:

- **Pure** (filter, formatter, level_filter, tag_annotator,
  json_encoder). Emit the same message with possibly different
  tags/payload but **same channel**. Do not touch `route_trace`.
- **Retargeting** (fan_out, re-publisher). Emit a new message on a
  **different channel**. Must append the new target channel to
  `route_trace` and check-before-dispatch per §20.1.
- **Terminal** (sinks). Do not re-emit; `route_trace` and
  `hops_remaining` are write-once and never checked.

The router enforces the append-and-check; transform authors do not
have to implement it themselves. Transforms that bypass this
contract (by constructing a brand-new `Message` from scratch
instead of mutating the passed-in one) effectively reset cycle
detection — this is sometimes deliberate (a metrics aggregator
emits one summary message per window, unrelated to inputs) and
sometimes a footgun. We mark the `Message` copy constructor as
*taking the trace along* by default, and provide an explicit
`Message::fresh()` factory for the deliberate-reset case.

### 20.6 Static route-graph validation (best effort)

When routes are registered at init time with
`add_route(RouteSpec)`, Manifold walks the known route graph and
warns on **configured cycles**:

- Build a directed graph: nodes are channels, edges are
  retargeting transforms and sink-initiated republishes.
- Run SCC detection (Tarjan's) on the graph.
- Any non-trivial SCC (> 1 node, or self-loop) is a configured
  cycle. Log to `etil.logging.warn` with the cycle nodes.

This is **warning, not prevention**. Configured cycles are
sometimes intentional (two-way bridges, echo-back scenarios). The
runtime checks in §20.1–§20.3 remain authoritative. Static
analysis just flags surprises early.

Not all cycles are discoverable statically — cycles introduced by
dynamic TIL `channel-route-add` calls, cycles that span broker
boundaries, and cycles involving conditional transforms can only
be caught at runtime.

### 20.7 TIL introspection

For debugging and tests:

```
channel-trace         ( -- array )   # current message's route_trace
channel-hops-left     ( -- n )       # current message's hops_remaining
channel-cycle-stats   ( -- map )     # aggregate: cycles detected,
                                     # TTL exhausted, echo drops
```

During evolution benchmarks or MCP request handling, a TIL test can
verify its own topology doesn't generate cycles by publishing a
probe message and checking `channel-cycle-stats` before/after.

### 20.8 Configuration knobs

Per-service config (not per-message):

```cpp
struct CycleConfig {
    bool enable_trace_check      = true;   // layer 1
    bool enable_ttl_check        = true;   // layer 2
    bool enable_echo_suppression = true;   // layer 3 (broker)
    uint8_t default_hops         = 32;     // starting TTL
    size_t max_trace_entries     = 32;     // cap on route_trace growth
    bool static_validate_on_add  = true;   // warn on route_add
};
```

All three layers ON by default. Operators can turn off any of them
for debugging but not silently — disabling any layer emits a
startup warning on `etil.logging.warn`.

### 20.9 Testing strategy

- **Unit tests** per layer: construct minimal topology that exercises
  each detection path; verify the correct audit channel fires and
  the message is dropped.
- **Property test**: generate random route graphs of N ≤ 20 channels
  with random transforms, publish probe messages, assert that any
  graph with an SCC either hits the static warning OR triggers a
  cycle audit on the first probe that enters the cycle.
- **Broker integration test** (Phase 3): ETIL process publishes
  to NATS, subscribes same topic; verify `reject_own_origin`
  drops the echo; flip the flag; verify the echo is delivered
  with `echo=true` tag.
- **TTL stress test**: force a pathological fan_out → fan_out chain
  that never revisits the same channel (evading layer 1); verify
  TTL catches it at hop 32.

### 20.10 Relationship to the rest of the design

Cycle detection touches:

- **§3 Message** — adds `route_trace` and `hops_remaining` fields
  (already updated above).
- **§4 Sinks** — broker sinks default to `reject_own_origin = true`.
- **§5 Transforms** — retargeting transforms must go through the
  router's `publish_from_transform()` path, not raw `publish()`,
  so trace append and check happen automatically.
- **§15 RBAC** — adds two hard-wired audit channels
  (`etil.aaa.audit.channel.cycle-detected`,
  `etil.aaa.audit.channel.ttl-exhausted`) to §15.5.
- **§16 Broker sinks** — echo suppression is a named option on
  every broker sink config; opt-out is explicit.
- **§18 Message identity** — origin tuple is the primary signal for
  layer-3 echo suppression.
- **§19 etil-web** — BroadcastChannel and postMessage routes are
  especially susceptible to local cycles; layers 1+2 apply
  unchanged in the browser.

The performance cost is negligible in the common path (single
publish to single sink: trace is one string, no scan needed since
the target channel isn't yet in the trace). In pathological cases
the checks are bounded at 32 entries. No deployment should ever
turn cycle detection off in production.

---

## 21. Summary of proposed TIL words

All words new to Manifold unless marked *unchanged*. Stack effect uses
standard FORTH notation `( before -- after )` with TOS on the right.
Section references link each word back to its design justification.

### 21.1 Core channel operations (§7)

| Word                     | Stack effect                                 | Notes                                    |
| ------------------------ | -------------------------------------------- | ---------------------------------------- |
| `channel-publish`        | `( msg channel-name -- )`                    | Publish a message on a named channel     |
| `channel-subscribe`      | `( channel-pattern -- observable )`          | Returns observable; RBAC Read-gated      |
| `channel-route-add`      | `( route-spec -- handle )`                   | Requires `channels_route_admin`          |
| `channel-route-remove`   | `( handle -- )`                              | Requires `channels_route_admin`          |
| `channel-tap-file`       | `( channel-pattern path -- handle )`         | Sugar for a file-sink route              |
| `channel-tap-udp`        | `( channel-pattern port host -- handle )`    | Requires `channels_network_sink`         |
| `channel-tap-observable` | `( channel-pattern -- observable )`          | Sugar for an observable-sink route       |
| `channel-list`           | `( -- array )`                               | Channels this role can introspect        |
| `channel-list-routes`    | `( -- array )`                               | Routes this role can introspect          |

### 21.2 RBAC / permissions (§15.8)

| Word                    | Stack effect                                  | Notes                                  |
| ----------------------- | --------------------------------------------- | -------------------------------------- |
| `role-grant-channel`    | `( actions pattern role-name -- )`            | `role_admin` only                      |
| `role-revoke-channel`   | `( pattern role-name -- )`                    | `role_admin` only                      |
| `role-channel-enable!`  | `( bool role-name -- )`                       | Master on/off switch; `role_admin`     |
| `role-network-sink!`    | `( bool role-name -- )`                       | `role_admin` only                      |
| `channel-perm-check`    | `( action channel-name -- bool )`             | Self-introspection; any role           |
| `channel-perm-list`     | `( -- array )`                                | Self-introspection; any role           |

### 21.3 MCP SSE (§17.3)

| Word                      | Stack effect                               | Inbound channel                     |
| ------------------------- | ------------------------------------------ | ----------------------------------- |
| `mcp-on-notification`     | `( method-pattern -- observable )`         | `etil.mcp.in.notification.**`       |
| `mcp-on-progress`         | `( -- observable )`                        | `etil.mcp.in.progress`              |
| `mcp-on-cancelled`        | `( -- observable )`                        | `etil.mcp.in.cancelled`             |
| `mcp-on-roots-changed`    | `( -- observable )`                        | `etil.mcp.in.roots.changed`         |
| `mcp-on-request`          | `( method-pattern -- observable )`         | `etil.mcp.in.request.*`             |

### 21.4 Message identity introspection (§18.8)

| Word                      | Stack effect                                          | Notes                                |
| ------------------------- | ----------------------------------------------------- | ------------------------------------ |
| `channel-origin`          | `( -- host-str startup-us session-str )`              | Process + session identity           |
| `channel-seq`             | `( -- seq )`                                          | Next sequence counter value          |
| `channel-last-published`  | `( -- msg-id-str )`                                   | Most recent publish on this session  |

### 21.5 Cycle detection, drop visibility, and debug (§20.7, §22.2)

| Word                     | Stack effect              | Notes                                                           |
| ------------------------ | ------------------------- | --------------------------------------------------------------- |
| `channel-trace`          | `( -- array )`            | Current message's `route_trace` (during handler)                |
| `channel-hops-left`      | `( -- n )`                | Current message's `hops_remaining` (during handler)             |
| `channel-cycle-stats`    | `( -- map )`              | Aggregate: cycles, TTL exhaustions, echo drops                  |
| `channel-sink-stats`     | `( route-handle -- map )` | Per-route drop counters, buffer depth, inline-vs-ring flag      |
| `channel-all-sink-stats` | `( -- array )`            | Snapshot of every active route's sink stats — §22.2             |

### 21.6 Browser-specific (§19.10, etil-web only)

| Word                    | Stack effect                                 | Channel / API                         |
| ----------------------- | -------------------------------------------- | ------------------------------------- |
| `browser-notify`        | `( body-str title-str -- ok? )`              | Web Notifications API                 |
| `browser-broadcast`     | `( msg-str name-str -- )`                    | `BroadcastChannel`                    |
| `browser-postmessage`   | `( msg-str target-origin-str -- )`           | `window.postMessage` with allowlist   |
| `browser-storage-put`   | `( value-str key-str -- )`                   | IndexedDB / OPFS                      |
| `browser-on-console`    | `( level-str -- observable )`                | `etil.web.console.{level}`            |
| `browser-on-error`      | `( -- observable )`                          | `etil.web.error.uncaught`             |
| `browser-on-message`    | `( -- observable )`                          | `etil.web.postmessage.in.*`           |

### 21.7 Existing words — signatures unchanged, wiring rerouted (§17.2)

| Word                    | Stack effect                    | Change                                          |
| ----------------------- | ------------------------------- | ----------------------------------------------- |
| `sys-notification`      | `( msg -- )`                    | Publishes to `etil.mcp.out.notification.system` |
| `user-notification`     | `( msg user-id -- ok? )`        | Publishes to `etil.mcp.out.notification.user`   |

### 21.8 Counts and phasing

- **35 new words** across six categories (core 9, RBAC 6, MCP SSE 5,
  identity 3, cycle + drop-visibility 5, browser 7).
- **2 existing words** preserved with identical signatures; internal
  wiring rerouted through Manifold channels.
- **Phasing alignment**:
  - *Phase 1*: §21.1 core (first six words), §21.2 RBAC (all six),
    §21.4 identity (all three), §21.5 cycle (all three), §21.7
    existing (rewired).
  - *Phase 2*: remaining §21.1 introspection, §21.3 MCP SSE (all
    five), §21.6 browser (console + error sources, Phase 1b).
  - *Phase 3*: §21.1 network-sink taps (`channel-tap-udp` gated by
    broker landing), rest of §21.6 browser (broadcast / postmessage /
    storage, Phase 2b) and WebSocket-backed remote integrations
    (Phase 3b).

No single phase introduces more than ~12 words, which keeps docs,
help text, and tests manageable per release.

---

## 22. Summary of open questions

All questions raised across the doc, consolidated for a single-pass
review. The **Inclination** column captures the current recommended
direction from the section where the question was raised; the
**Answer** column is intentionally left blank for the decision record.

| #   | §         | Question                                                                                                                      | Inclination                                                              | Answer                                                                                                             |
| --- | --------- | ----------------------------------------------------------------------------------------------------------------------------- |--------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------|
| Q1  | §13, §14  | `std::any` payload vs a closed type hierarchy?                                                                                | `std::any`; benchmark; specialize only if hotspots appear                | Y (Y == accept inclination)                                                                                        |
| Q2  | §13       | Per-route backpressure when a slow sink lags — block / drop-oldest / drop-newest / buffer?                                    | Drop-tail default with periodic "dropped N" summary                      | See Answer A1 below.                                                                                               |
| Q3  | §13       | Threading model: shared pool, dedicated thread per slow sink, or inline on producer?                                          | Mix: file sinks inline; network sinks on a shared pool                   | Y                                                                                                                  |
| Q4  | §13       | Schema evolution — how does a consumer handle a new tag or changed payload type?                                              | Backwards-compatible tag set + versioned payload types — TBD             | Y (We can add a tag deprecation channel in the future!)                                                            |
| Q5  | §13       | Verify the "zero cost when channel has no routes" claim holds under real publish load.                                        | Benchmark at Phase 1 with a no-subscriber hot loop                       | Y                                                                                                                  |
| Q6  | §14       | Formalize channel naming convention (`etil.<module>.<event>.<detail>`) before subsystems start publishing.                    | Lock the top-level subtree names in Phase 1 docs                         | Y, with an optional version .aka. `etil.<module>.<event>.<detail>[.version]` wher version is a integer or a semver |
| Q7  | §14       | EvolveLogger — keep as separate subsystem or absorb into Manifold as sinks on `etil.evolution.**`?                            | Keep separate until Phase 4; re-evaluate after source migration          | Integrate and absorb. Mask bits convert to new channels IMO then go away.                                          |
| Q8  | §14       | Dependency injection: raw constructor injection or a container like `boost::di`?                                              | Raw constructor injection (ETIL subsystem count is small)                | Y                                                                                                                  |
| Q9  | §14       | Library linkage: static only, or shared to support plugin-style loading by TUI / external tools?                              | Static; revisit only if a plugin consumer appears                        | Y                                                                                                                  |
| Q10 | §16.10    | Primary broker target: Apache Artemis (ASF, AMQP 1.0) or NATS + JetStream (CNCF, Apache-2.0, lighter footprint)?              | Ship both `amqp_sink` and `nats_sink`; operator picks per deploy         | Y                                                                                                                  |
| Q11 | §17.8     | Outbound SSE overflow policy per route — `drop_oldest` / `drop_newest` / `block`?                                             | Per-route config; `drop_oldest` default for notifications                | Same as backpressure.                                                                                              |
| Q12 | §17.8     | SSE reconnect: re-subscribe and replay missed messages, or start fresh?                                                       | Broker-backed routes: replay. In-process: fresh. Document asymmetry      | Y                                                                                                                  |
| Q13 | §18.7     | Extend `MessageOrigin` with PID to disambiguate same-microsecond startup collisions on one host?                              | Not needed by default; add only if observed in production                | Y                                                                                                                  |
| Q14 | §19.9     | spdlog in WASM — keep the native backend or replace with a `console_sink`-only minimal backend to reduce binary size?         | Measure WASM binary-size impact before deciding                          | Replace, spdlog little value in web context.                                                                       |
| Q15 | §19.11    | Broker-sink behavior when the page is offline (Service Worker) — fail hard, queue, or fall back to `indexeddb_sink`?          | Graceful fallback to `indexeddb_sink` via a route-fallback transform     | Y                                                                                                                  |
| Q16 | §20.8     | Cycle-detection default configuration — all three layers (trace / TTL / echo) on by default?                                  | All ON; disabling any layer emits a startup warning                      | Y                                                                                                                  |
| Q17 | §19.8     | etil-web default permission mode when standalone vs when connected to a remote MCP server.                                    | Standalone: all-permitted. Connected: server enforces cross-boundary ops | Y                                                                                                                  |
| Q18 | §11       | Android Intent upper layer — land as part of the initial library, or defer until concrete cross-subsystem events materialize? | Defer; ship the channel transport first                                  | Y                                                                                                                  |

#### Answer A1:
- Default: buffer with drop-first. 
- Optional drop-last mode. 
- Fixed buffer sizes: Small ... Like 16 because the payloads can be huge. 
- inline-on-producer for etil.aaa.audit.** and etil.security.** to avoid dropping issues.

### 22.1 Decision-making sequence

Questions are not all due at the same time. Rough ordering:

- **Before Phase 1 starts**: Q1, Q6, Q8, Q9, Q16 (these lock core API
  shape and build scaffolding; late changes are disruptive).
- **During Phase 1**: Q2, Q3, Q5 (backpressure, threading, zero-cost
  validation — answer with benchmarks while building).
- **During Phase 2**: Q4, Q7 (schema evolution once real payloads
  exist; EvolveLogger merger decision when source migration is hot).
- **Before Phase 3 starts**: Q10, Q11, Q12 (broker selection and
  SSE-specific backpressure/reconnect — affects broker sink API).
- **During Phase 3b (etil-web broker)**: Q14, Q15 (WASM size
  measurements and offline fallback — inform the CMake matrix).
- **Deferred indefinitely unless triggered**: Q13 (PID tiebreak —
  reactive; only add when a collision is observed), Q17 (etil-web
  RBAC mode — already has a sensible default), Q18 (Intent layer —
  wait for cross-subsystem need).

No question blocks Phase 0 (logging substrate in survey doc A).

### 22.2 Sink overflow and delivery guarantees

Consequence of Answer A1 ("never block") spelled out across the
affected design areas, so Phase 1 implementation has a complete
specification to work from.

**Sink delivery modes.** Every sink is declared as one of:

- **`RingBuffered`** (default) — fixed-size ring, configurable
  capacity (default 16), overflow policy `drop_first` (default) or
  `drop_last`. Drained by the sink's configured thread
  (inline/pool/dedicated per §13 Q3).
- **`Inline`** — no buffer. The producer's `publish()` call drives
  the sink's `accept()` synchronously on the producer thread.
  Producer latency is bounded by the sink's write time (typically a
  file or local-socket write — fast). Used for hard-wired audit,
  security, bootstrap, and logging-error channels where any drop
  is a correctness failure.
- **`InlineBounded`** — rare intermediate. Inline delivery but with
  a short timeout (microseconds) after which the message is
  dropped-with-record rather than stalling the producer
  indefinitely. Not in the default sink palette; available for
  cases where inline is needed but the sink can conceivably wedge
  (e.g., a fsync on a troubled disk).

**Drop visibility.** Because producers never block and never know
about drops, visibility is a first-class deliverable:

- **Per-route drop counters** are maintained atomically. Readable
  via `channel-sink-stats` and `channel-all-sink-stats` (§21.5).
- **Periodic drop summaries** are emitted on
  `etil.manifold.sink.drops` every N seconds (configurable,
  default 10) with `{route_handle, channel_pattern, dropped_count,
  since_timestamp}` payload. Operators subscribe here to get
  actionable alerts without polling.
- **First-drop and burst-threshold events** emit on
  `etil.manifold.sink.drop-event` once when a previously-clean
  route has its first drop, and again when drop rate crosses a
  configurable burst threshold. These are actionable "something
  changed" signals; the periodic summary is for trend tracking.
- **No silent drops anywhere.** If a sink drops and the drop
  wasn't recorded, that is a Manifold bug, not a design choice.

**Hard-wired channels and inline delivery.** The
`kHardwiredChannels` table (§15.5) carries a delivery-mode column
per entry. The following channels are `Inline`:

- `etil.aaa.audit.**` — all AAA audit records, including the
  channel-permission denial / cycle-detection / TTL-exhaustion
  audit entries that Manifold itself generates.
- `etil.security.**` — Falco-style alerts, AAA failure records.
- `etil.system.bootstrap.**` — startup/shutdown events before the
  buffered path is up (chicken/egg) and after it is down.
- `etil.logging.error` — the safety-valve error channel.

Non-hard-wired channels default to `RingBuffered`; operators or
TIL code can change a route's delivery mode via
`channel-route-add` with an explicit `DeliveryMode` field in the
`RouteSpec`.

**Producer-context notes.**

- **Interpreter thread publishing.** A TIL word that publishes onto
  a `RingBuffered` route returns immediately regardless of sink
  state. On an `Inline` route it waits for the sink's `accept()`
  to complete — for audit/security channels this is a file-append,
  typically sub-millisecond.
- **libuv I/O thread publishing.** Also never blocks on
  `RingBuffered` routes. `Inline` routes are safe only if the
  inline sink is genuinely fast (file append). Network sinks
  **must not be `Inline`** when invoked from an I/O thread;
  Manifold enforces this by refusing `Inline` mode on network
  sinks at route-add time (returns an error).
- **Evolution benchmarks.** A 10k msg/sec producer into a 16-entry
  ring with a 1k msg/sec sink drops ~90% by design. For
  replay-fidelity capture, use a `file_sink` with `Inline`
  delivery — the file system (not the ring) is the throughput
  bottleneck, and `fprintf`-like bursts are well-tolerated by any
  local disk. No drops, bounded producer latency.

**Broker-sink delivery failure.** Broker sinks (§16) cannot be
`Inline` — round-trip across the wire is orders of magnitude
slower than tolerable producer latency. Broker sinks are always
`RingBuffered`. When the broker is unreachable:

1. The send attempt fails; the message drops and the drop counter
   increments.
2. A one-shot event fires on
   `etil.manifold.sink.broker-unreachable` with the route handle
   and error detail. Subsequent failures within a cooldown window
   do not re-fire this event (prevents alert storms).
3. When the broker reconnects, a
   `etil.manifold.sink.broker-reconnected` event fires.
4. There is **no in-sink retry queue**. Operators who want retry
   semantics attach a dedicated retry transform upstream, or use
   the route-fallback pattern from §19.11 (offline fallback to
   `indexeddb_sink` in the browser).

**Relationship to §3 `RouteSpec`.** Add two fields:

```cpp
struct RouteSpec {
    std::string channel_pattern;
    absl::flat_hash_map<std::string, std::string> tag_filter;
    std::vector<std::shared_ptr<ITransform>> transforms;
    std::shared_ptr<ISink> sink;

    // §22.2 delivery mode (new fields)
    enum class DeliveryMode { RingBuffered, Inline, InlineBounded };
    DeliveryMode delivery = DeliveryMode::RingBuffered;
    size_t buffer_capacity = 16;              // ignored unless RingBuffered
    enum class OverflowPolicy { DropFirst, DropLast };
    OverflowPolicy overflow = OverflowPolicy::DropFirst;
};
```

Default-constructed `RouteSpec` therefore gets `RingBuffered / 16 /
DropFirst`, matching Answer A1. Hard-wired channels register
themselves with `Inline` explicitly.

### 22.3 Ambiguity resolution log

Original entries updated with their Phase 1-2 resolutions. Four of six
items (A-1, A-3, A-4, A-6) are fully resolved and shipped; A-2 and A-5
remain deferred to post-sprint work.

- **A-1 — Channel name version-segment format. RESOLVED (Phase 1a,
  commit 57159fc): integer-only.** No dotted-semver support.
  `etil.foo.bar.2` is a legal versioned channel; `etil.foo.bar.2.0.0`
  is not — parsed as four segments, no special version handling.
  Wildcards (`*` / `**`) match version segments as normal segments.
  Enforced by `validate_channel_name()` in
  `include/etil/manifold/channel_name.hpp`.

- **A-2 — EvolveLogger absorption mechanics.** DEFERRED to a dedicated
  post-sprint migration. The 105 EvolveLogger call sites across five
  files need a separate plan covering: (a) the 17-bit-category →
  channel-name mapping, (b) the fate of the four `evolve-log-*` TIL
  words (keep as convenience wrappers vs delete and require
  `channel-publish`), (c) migration sequence (big-bang vs dual-
  running), and (d) impact on existing benchmark scripts that parse
  EvolveLogger's per-run files directly.

- **A-3 — spdlog-in-WASM foundation story. RESOLVED (Phase 0, commit
  52a0913): dual-backend façade.** `src/core/logging.{hpp,cpp}`
  presents a single public API — `logging::get(name)` returns a
  `std::shared_ptr<spdlog::logger>` on both backends. Native links
  spdlog with a rotating file sink + stderr sink for WARN+. WASM
  (`ETIL_WASM_BUILD`) skips the file sink and relies on Emscripten's
  built-in stdout/stderr → `console.log`/`console.error` mapping.
  Callers see the same `->info()` / `->warn()` / `->error()` surface
  in both builds. The 41 raw-stderr sites from survey doc A were
  migrated to this façade, not to parallel per-build paths.

- **A-4 — MessageOrigin.hostname format divergence. RESOLVED
  (Phase 1a, commit 57159fc): accept divergence; add `origin_type`
  discriminator.** Native fills `hostname` from `gethostname()`;
  browser fills it from `location.origin`. Every `MessageOrigin` also
  carries an `origin_type: {Native, Browser}` enum field so
  cross-deployment consumers can discriminate without string-parsing
  hostname. The enum is propagated through broker serialization
  (§16.5) and exposed in the TIL `channel-origin` introspection word
  (see A-6).

- **A-5 — Broker-backed subscribers and session scoping.** DEFERRED to
  Phase 3. Not applicable until broker sinks land — the in-process
  SSE sinks already filter by `session_id` per §17 and Phase 1b's
  implementation (commit 3530862). Resolution will accompany the
  `amqp_sink` / `nats_sink` work in Phase 3: default policy
  (session-scoped with opt-in broadcast, vs broadcast with opt-in
  session-scope), the config surface for the override, and audit
  behaviour when a monitor-style sink receives cross-user payloads.

- **A-6 — `channel-origin` return shape. RESOLVED (Phase 2a, commit
  d80123a): HeapMap with alphanumeric keys.** `channel-origin ( -- map )`
  returns a single `HeapMap` with keys `host` (string), `startup`
  (int microseconds), `session` (string), and `origintype` (string:
  `"native"` or `"browser"`). Three stack items was too much for a
  single introspection word. The separate
  `channel-seq ( -- seq )` and
  `channel-last-published ( -- msg-id-str )` words remain single-
  value returns per the plan.

---

## 23. Status

Design exploration. No code written. Survey companion doc (A) is the
prerequisite — the `logging` module must exist before this library can
use it as a back-end sink. The RBAC design in §15 extends the existing
`RolePermissions` struct, so it lands as part of Phase 1 of the
channel library rollout (not before — Phase 0 logging does not need
channel RBAC). Broker integration (§16) is Phase 3+ with Artemis and
NATS as the two candidate targets.

Next concrete action is **none until Phase 0 ships.** Get the logging
policy enforced and the 41 stderr sites migrated, then come back to
this document for Phase 1 scoping with §15 (RBAC), §18 (message
identity / origin tuple), and §20 (cycle detection: route_trace,
hop-budget TTL, origin echo suppression) as Phase 1 deliverables,
§17 Phase A as a Phase 1 sub-task (outbound-channel refactor of
sys-notification / user-notification wiring), §17 Phase B as
Phase 2, §16 + §17 Phase C as Phase 3 follow-ups, and §19
browser-facing sinks/sources as etil-web Phase 1b / 2b / 3b / 4b
in parallel with the native rollout.