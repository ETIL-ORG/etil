# Logging Infrastructure Survey

**Date:** 2026-04-18
**Version surveyed:** v2.3.11
**Purpose:** Catalog every output channel (stdout, stderr, spdlog, EvolveLogger, file I/O)
so we can plan a coherent logging overhaul. Current pain point: during long evolution
runs (P1 validation, Phase 5a prep) it is difficult to follow what is happening because
diagnostic output is scattered across three unrelated systems and a large amount of raw
`fprintf(stderr)` that is invisible once you redirect the REPL.

---

## Executive summary

There are **three disjoint output systems** in the codebase today:

| System | Sites | Files | Status |
|---|---|---|---|
| **spdlog** (centralized) | 2 | 2 | Linked but effectively unused |
| **Raw `fprintf(stderr)` / `std::cerr`** | ~41 in `src/` (70 codebase-wide) | 7 source files | Primary error-reporting path |
| **EvolveLogger** (purpose-built) | 105 call sites | 5 files | Rich, 17-category, evolution-only |

The interpreter/REPL layer has a clean abstraction (`ExecutionContext::out_` / `err_`)
so language output is already redirectable. The pain is **outside** the interpreter —
the MCP server, OAuth flows, MongoDB client, and AAA layer all write directly to
`stderr` with no level control, no categories, no file persistence, and no spdlog
integration. During a 100-generation evolution benchmark, errors from any of these
subsystems land in the same uncaptured stderr stream as REPL startup banners and
evolution diagnostics, and there is nothing durable to grep after the fact.

EvolveLogger is architecturally sound but intentionally isolated: its header comment
reads *"Logs are NOT mixed with spdlog, interpreter output, or MCP streams."* That
isolation is worth preserving for per-run evolution forensics, but the non-evolution
subsystems need their own structured logger.

---

## 1. spdlog usage — linked but dormant

spdlog is in the dependency graph (`src/CMakeLists.txt:93, 106`) and included in
two files. Only two live call sites exist.

### Call sites

| File:Line | Call | Level | Context |
|---|---|---|---|
| `src/core/dictionary.cpp:35` | `spdlog::debug("Registered implementation for concept '{}'", word);` | DEBUG | Fires on every word registration — very noisy if DEBUG is enabled |
| `src/mcp/mcp_server.cpp:270` | `spdlog::info("Evicting oldest session {} for user {} ...");` | INFO | Session eviction when per-user limit hit |

### What is missing

- **No logger factory.** Neither file creates named loggers; both use the global
  default logger.
- **No sink configuration.** No `basic_file_sink`, `rotating_file_sink`, or async
  sink is ever instantiated. Output goes to spdlog's default (stderr) with the
  default pattern.
- **No level control.** No `spdlog::set_level(...)` call anywhere in the codebase.
  No env var, no CLI flag, no TIL word.
- **No named loggers per subsystem.** All logging, if it existed, would share a
  single global logger.

### Recommendation

spdlog is the obvious home for the 41 raw `fprintf(stderr)` calls in MCP / AAA /
DB code. The overhaul should add a `src/core/logging.{hpp,cpp}` that provides a
logger factory, a rotating file sink, runtime level control, and a common log
pattern. EvolveLogger stays separate (see §4).

---

## 2. Raw `stderr` writes — the main problem surface

Within `src/` there are **41 `fprintf(stderr, ...)` call sites in 7 files**. All of
them are legitimate informational or error output that wants a home. None are
debug prints that should simply be deleted.

### 2.1 `src/mcp/mcp_server.cpp` (12 sites)

| Line | Content | Proposed level |
|---|---|---|
| 70 | `"JWT authentication enabled (config: %s)"` | INFO (startup) |
| 73 | `"Warning: auth config loaded but JWT keys ..."` | WARN |
| 83 | `"OAuth provider enabled: github"` | INFO |
| 86 | OAuth GitHub warning | WARN |
| 94 | `"OAuth provider enabled: google"` | INFO |
| 96 | `"Warning: unknown OAuth provider '%s' ..."` | WARN |
| 101 | `"Warning: failed to load auth config '%s': %s"` | WARN |
| 115 | `"Warning: MongoDB connection failed"` | WARN |
| 126 | `"Warning: MongoDB AAA connection failed"` | WARN |
| 136 | `"AAA database: %s"` | INFO |
| 499 | `"MCP server error: %s"` (request exception) | ERROR |
| 503 | `"MCP server unknown error"` (request exception) | ERROR |

### 2.2 `src/mcp/http_transport.cpp` (4 sites)

| Line | Content | Proposed level |
|---|---|---|
| 87 | `"ETIL MCP server listening on %s:%d"` | INFO |
| 98 | `"ETIL MCP server listening on %s:%d (multi-session)"` | INFO |
| 759 | `"MCP handler exception: %s"` | ERROR |
| 763 | `"MCP handler unknown exception"` | ERROR |

### 2.3 `src/db/mongo_client.cpp` (16 sites)

All 16 are exception handlers reporting MongoDB failures (connect, find, count,
insert, update, delete, index creation). Currently silent if stderr is redirected.

| Lines | Content | Proposed level |
|---|---|---|
| 125, 128 | Config load status / failure | INFO / WARN |
| 182 | `"MongoDB connected: %s ..."` | INFO |
| 186 | `"MongoDB connection failed: %s"` | ERROR |
| 211, 228 | `ensure_unique_index` / `ensure_ttl_index` error | ERROR |
| 266, 269, 292, 295, 314, 317, 343, 346, 369, 372 | Operation exceptions (find/count/insert/update/delete, both cxx and JSON variants) | ERROR |

### 2.4 `src/mcp/oauth_github.cpp` (2 sites)

| Line | Content | Proposed level |
|---|---|---|
| 36 | `"GitHub device code request failed: %s"` | WARN |
| 53 | `"GitHub device code error: %s"` | WARN |

### 2.5 `src/mcp/oauth_google.cpp` (3 sites)

| Line | Content | Proposed level |
|---|---|---|
| 34 | `"Google device code request failed: %s"` | WARN |
| 43 | `"Google device code: invalid JSON response"` | WARN |
| 48 | `"Google device code error: %s"` | WARN |

### 2.6 `src/aaa/audit_log.cpp` (1 site)

| Line | Content | Proposed level |
|---|---|---|
| 53 | `"AuditLog error: %s"` | ERROR |

### 2.7 `src/aaa/user_store.cpp` (3 sites)

| Line | Content | Proposed level |
|---|---|---|
| 102 | `"UserStore find_by_email error: %s"` | ERROR |
| 138 | `"UserStore create error: %s"` | ERROR |
| 165 | `"UserStore record_login error: %s"` | ERROR |

### Common pattern and concern

Every one of these sites is wrapped around an `absl::Status` or `std::exception`
and prints *only* the `.what()` or status message — no stack context, no request
ID, no session ID, no subsystem tag. When Mongo fails during a Docker run, the
one-line message ends up interleaved with benchmark output and is nearly useless
for post-mortem.

**Recommendation:** replace all 41 with spdlog calls on a named logger
(`etil.mcp`, `etil.db`, `etil.aaa`) once the factory exists. The migration is
mechanical: `fprintf(stderr, fmt, args...)` → `spdlog::get("etil.mcp")->error(fmt, args...)`.

---

## 3. `stdout` and `stderr` in REPL, examples, and tests

This is correctly partitioned and should be **left alone**.

### 3.1 REPL — `examples/simple_repl.cpp`

- Writes user-facing interactive content to `stdout` (version banner, prompt, stack
  display, help, goodbye). These are not log entries.
- Writes config-load warnings to `stderr` (lines 75, 95) — could move to spdlog but
  low priority; runs before logger is initialized.
- Uses `ctx.set_out(&std::cout)` / `ctx.set_err(&std::cerr)` to redirect interpreter
  output. This is the **correct abstraction**: the interpreter does not write to any
  stream directly, it writes to its `ExecutionContext` streams.

### 3.2 `include/etil/core/execution_context.hpp:316,497`

The clean abstraction already exists:

```cpp
std::ostream* out_ = &std::cout;
std::ostream* err_ = &std::cerr;
```

Tests (`test_compiled_body.cpp`, `test_interpreter.cpp`, etc.) use
`ctx.set_out(&stream)` / `ctx.set_err(&stream)` to capture interpreter I/O.
GoogleTest's `CaptureStdout()` / `GetCapturedStdout()` is used for testing
primitives that write to stdout (26 call sites in `test_primitives.cpp` alone).

### 3.3 Examples

- `examples/mcp_server.cpp` — 8 `std::cerr` sites, all CLI argument parsing /
  startup diagnostics. Runs before server starts; moving to spdlog would require
  initializing the logger before argument parsing, which is awkward. Leave.
- `examples/benchmark.cpp` — uses Google Benchmark's own output mechanism.

### 3.4 REPL stderr/stdout is already coherent

**No action needed.** The language runtime and REPL have a well-defined I/O
model. The problem is strictly at the subsystem layer (MCP, DB, AAA, OAuth).

---

## 4. EvolveLogger — the purpose-built evolution logger

Defined in `include/etil/evolution/evolve_logger.hpp` and `src/evolution/evolve_logger.cpp`.
Header comment: *"Logs are NOT mixed with spdlog, interpreter output, or MCP streams.
Each start() opens a new file: YYYYMMDDThhmmss-evolve.log"*.

### 4.1 Architecture

- **Lifecycle**: `start(level, category_mask)` opens a timestamped file in the
  configured directory; `stop()` closes it; destructor closes if open.
- **Levels**: `Off`, `Logical` (narrative), `Granular` (function-level with
  parameters). When `Off`, `enabled()` returns false for zero overhead.
- **Categories**: 17-bit mask. The header comment says "14-category" but the enum
  has grown to 17: `Engine`, `Decompile`, `Substitute`, `Perturb`, `Move`,
  `ControlFlow`, `Grow`, `Shrink`, `Crossover`, `Repair`, `Compile`, `Fitness`,
  `Selection`, `Bridge`, `Diff`, `ASTDump`, `DAG`. (Header doc comment should be
  corrected as a cleanup.)
- **Flushing**: every `log()` / `detail()` call flushes; no buffering loss on crash.
- **File format**: `[timestamp] [category-tag] message\n`.
- **Exposed stream**: `std::ostream* stream()` returns the underlying `ofstream`
  if open. Comment at line 102 says *"Access the underlying file stream (for
  redirecting error output)"* — indicates this was designed with cross-cutting
  use in mind, but nothing currently uses it for non-evolution output.

### 4.2 TIL control surface (`src/core/primitives.cpp`)

- `evolve-log-start ( level category-mask -- )` → `prim_evolve_log_start` (line 1970)
- `evolve-log-stop ( -- )` → `prim_evolve_log_stop` (line 1997)
- `evolve-log-dir ( path -- )` → `prim_evolve_log_dir` (line 2008)
- `evolve-log-show-failed ( bool -- )` → `prim_evolve_log_show_failed` (line 1958)

Disabled by default; must be turned on per benchmark run.

### 4.3 Call sites — 105 total across 5 files

| File | Calls | Role |
|---|---|---|
| `src/evolution/ast_genetic_ops.cpp` | 57 | Mutation operators (substitute, perturb, move, control flow, crossover, repair), AST diffs, before/after dumps |
| `src/evolution/evolution_engine.cpp` | 36 | Generation loop, child evaluation, selection/scheduling, DAG stats per generation, UCB contribution weights |
| `src/evolution/bridge_map.cpp` | 8 | Bridge candidate evaluation, insertion decisions |
| `src/evolution/evolve_logger.cpp` | 3 | Self-referential (timestamp, filename helpers) |
| `src/CMakeLists.txt` | 1 | Build-system reference |

### 4.4 What EvolveLogger does well

- **Per-run isolation.** A new log file per `start()` with timestamp name. Trivial
  to collect and archive per benchmark run.
- **Zero overhead when off.** `enabled(cat)` short-circuits on level check.
- **Category granularity.** Turn on just `DAG | Selection` to study scheduling
  without being drowned in mutation-level detail.

### 4.5 What EvolveLogger does not cover

- **Fitness per test case.** `ConceptNodeStats::record_fitness` is called 31
  times in `evolution_engine.cpp`, but the per-call value is only aggregated;
  nothing in the EvolveLogger output shows the per-test-case fitness trace. A
  new `EvolveLogCategory::Decision` (or expand `Fitness`) would help.
- **The actual scheduling decision.** UCB contribution computation in
  `evolve_dag_generation` is visible only via the end-of-generation DAG dump;
  the intermediate reasoning (exploit term, explore term, why this concept
  won this round) is not logged.
- **P1 diagnostic-print thrash.** During P1 I repeatedly added/removed `printf`
  calls from `evolution_engine.cpp` to trace the signal. That work should have
  gone into EvolveLogger under the `Selection` category from the start.

---

## 5. File I/O for non-logging purposes

These are application-logic file writes, **not** diagnostic channels, listed here
for completeness so they are not confused with logging.

| File:Line | Purpose |
|---|---|
| `src/lvfs/lvfs.cpp:360, 375` | LVFS write operations |
| `src/core/observable_primitives.cpp:743, 796` | Observable file persistence |
| `src/core/async_file_io.cpp:202` | Async file write wrapper |
| `src/mcp/tool_handlers.cpp:1060` | MCP tool output file |
| `src/mcp/auth_config.cpp:375` | Config file write |

Plus ~20 `std::ofstream` sites in `tests/` for fixture files.

---

## 6. MCP protocol and stdio transport

MCP stdio transport convention: `stdout` is reserved for JSON-RPC messages,
`stderr` is for server diagnostics. Today that convention is respected
*accidentally* because:

- The REPL and MCP server do not share a process.
- `examples/mcp_server.cpp` writes no JSON to stderr; argument parsing uses
  `std::cerr`.
- Subsystem errors use `fprintf(stderr)` (§2), which the MCP client sees as
  "server diagnostics" — fine for MCP, but it means the same stream carries
  every subsystem's warnings with no discrimination.

If we route `fprintf(stderr)` through spdlog with a file sink, MCP stdio's
stderr becomes quieter — which is correct. An operator reading live stderr
gets a clean feed; the durable record goes to a file.

No per-request logging exists today (no "request received", "request complete",
"request failed"). This is a gap for the AAA story in particular — audit entries
go to the Mongo `audit_log` collection, but there is no file-based log of
who-hit-what-endpoint for correlating with Falco alerts.

---

## 7. TUI logging

The TUI is a separate repository (`etil-tui`) and does not ship source with
ETIL. From CLAUDE.md: it has a `--log` flag (logging off by default as of recent
change). The TUI talks to ETIL over MCP, so:

- ETIL stdio stderr during a TUI session is consumed by the TUI's MCP
  transport layer and typically discarded — **not** shown in the terminal UI.
- Anything ETIL writes to `fprintf(stderr)` during a TUI session is effectively
  lost.

**This is why routing to a file matters.** Even a well-formatted stderr message
is invisible to an operator running the TUI. The overhaul's first practical win
is a persistent log file that outlives the stream.

---

## 8. Proposed overhaul — minimum viable redesign

Leave EvolveLogger alone. Fix the non-evolution side.

### 8.1 Add `src/core/logging.{hpp,cpp}`

```cpp
namespace etil::core::logging {
    void init(const std::string& log_dir, spdlog::level::level_enum default_level);
    void shutdown();
    std::shared_ptr<spdlog::logger> get(const std::string& name); // cached
}
```

Named loggers: `etil.mcp`, `etil.http`, `etil.db`, `etil.aaa`, `etil.oauth`,
`etil.dict`, `etil.session`. Combined rotating file sink +
stderr sink. Pattern: `[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v`.

### 8.2 Migrate the 41 `fprintf(stderr)` call sites

Mechanical replacement per §2. One commit per subsystem (mcp_server,
http_transport, mongo_client, oauth_*, audit_log, user_store) keeps review
manageable.

### 8.3 Add runtime level control

- Env var: `ETIL_LOG_LEVEL=warn|info|debug|trace` (read by `logging::init`).
- CLI flag: `--log-level <level>` on `mcp_server` executable.
- TIL word: `log-level! ( level-str -- )` for REPL control.
- TIL word: `log-dir! ( path -- )` to set log output directory before init.

### 8.4 Add per-request MCP logging

In `handle_request`: log method / user / session ID on entry, result / elapsed
on exit, exception on failure. Level INFO for normal; ERROR for failure.
Enables correlation with AAA audit entries and Falco alerts.

### 8.5 Extend EvolveLogger with a `Decision` category

Add `EvolveLogCategory::Decision = 1 << 17`. Log:
- UCB exploit + explore breakdown per concept per generation.
- Which child was selected by `SelectionEngine` and why.
- Per-test-case fitness when `Granular` level is active.

This is what P1 needed and what Phase 5a will need again.

### 8.6 Do not merge EvolveLogger and spdlog

They serve different audiences:

| | EvolveLogger | spdlog |
|---|---|---|
| Audience | Evolution researcher, post-run analysis | Operator, live monitoring |
| File lifetime | One file per `start()` / benchmark run | Rotating, weeks of retention |
| Overhead when off | Zero (`enabled()` short-circuit) | Near-zero (spdlog level filter) |
| Categories | 17 fine-grained evolution subsystems | Coarse loggers per module |
| Typical read pattern | `less YYYYMMDDThhmmss-evolve.log` | `tail -f etil.log` or grep |

Keep them distinct. One-line cross-reference in each (e.g., EvolveLogger logs
`"see etil.log for underlying MCP errors"`) is enough.

---

## 9. Deferred items (not blockers, but worth noting)

- **Header doc drift**: `evolve_logger.hpp:12` says "14-category" but enum has
  17 entries. Update comment.
- **`EvolveLogger::stream()` accessor is unused**: intended for redirecting
  "error output" but no caller exists. Either wire it up (e.g., route fitness
  evaluation errors there) or delete it.
- **REPL `std::cerr` sites (lines 75, 95 in `simple_repl.cpp`)**: low priority;
  these fire before any logger could be initialized.
- **`examples/mcp_server.cpp`**: startup argument parsing uses `std::cerr`.
  Can move to spdlog only after `logging::init` runs, which requires parsing
  args first for `--log-dir`/`--log-level`. Chicken/egg — leave as stderr.
- **Spdlog async sink**: file sink is thread-safe, but under heavy MCP load a
  synchronous sink may contend. Consider `spdlog::async_logger` once the
  migration is done and we have load measurements.

---

## 10. Measurement tables

### 10.1 Counts by channel type

| Channel | `src/` | `examples/` | `include/` | `tests/` |
|---|---|---|---|---|
| `spdlog::*` | 2 | 0 | 0 | 0 |
| `fprintf(stderr)` / `std::cerr` | 41 | 10+ | 2 (defaults) | 9 |
| `std::cout` / `printf` | 0 | 26 | — | 0 |
| EvolveLogger calls | 104 | 0 | 1 (header example) | 0 |
| `std::ofstream` (app logic) | ~10 | 0 | 0 | ~20 (fixtures) |

### 10.2 Raw stderr by subsystem

| Subsystem | Count |
|---|---|
| MongoDB client | 16 |
| MCP server lifecycle/auth/errors | 12 |
| MCP HTTP transport | 4 |
| OAuth (Google + GitHub) | 5 |
| AAA user store | 3 |
| AAA audit log | 1 |
| **Total (src/)** | **41** |

---

## 11. Policy — direct stdout/stderr logging is forbidden (effective 2026-04-18)

**Decision:** direct writes to `stdout` or `stderr` for the purpose of logging,
diagnostics, warnings, or errors are **no longer permitted** in ETIL source.
Effective now.

### 11.1 Scope

Applies to all code under `src/`, `include/`, `examples/`, and any new code
added to `tests/` beyond what is already present. The following APIs are
banned as logging channels:

- `fprintf(stderr, ...)`, `fputs(..., stderr)`, `fwrite(..., stderr)`, `perror(...)`
- `std::cerr << ...`
- `fprintf(stdout, ...)`, `printf(...)`, `puts(...)`
- `std::cout << ...`
- `fmt::print(stderr/stdout, ...)`, `std::print(stderr/stdout, ...)`

### 11.2 Exceptions — "out-of-band" bootstrap and shutdown errors

The *only* permitted direct-stderr writes are errors that occur when the
logger is **inactive or impossible to use**:

- **Process startup, before `logging::init()` has run.** Example: failure to
  parse the `--log-dir` argument that determines where the log file should go.
  There is no logger yet.
- **Process shutdown, after `logging::shutdown()` has run.** Example: static
  destructor failure, `atexit` handler.
- **Logger initialization failure itself.** Example: cannot open the log
  directory for writing. The error announcing "cannot log" obviously cannot
  be logged.

These bootstrap/shutdown errors go to `stderr` with a clear one-line format.
They are rare and should be brief. If a site is not one of the three cases
above, it must use a named spdlog logger.

### 11.3 REPL and language I/O are channels, not raw streams

Interpreter-emitted output (the TIL `.` word, `emit`, `cr`, etc.) and REPL UI
output (prompts, banners, help text) continue to flow today through
`ExecutionContext::out_` / `err_` and the REPL's local `std::cout`. That remains
the short-term behavior, but these are **also in scope** for the broader I/O
channel pipeline described in §12 and the sibling doc
`20260418B-IO-Channel-Pipeline-Architecture.md`. Under that design, REPL output
and MCP server output are named channels that can be redirected to arbitrary
sinks (UDP, file, observable, remote collector) without changing the producer.

For this immediate logging policy, the practical rule is the same: new code must
not write directly to `stdout`/`stderr`. If you are producing language output,
use the `ExecutionContext` streams. If you are producing diagnostic output, use
a named spdlog logger. If you are producing something that does not fit either
category — events, traces, structured observations — that is an argument for
moving to the channel pipeline rather than inventing a new raw-stream path.

### 11.4 EvolveLogger is unchanged

EvolveLogger remains the correct channel for evolution-pipeline diagnostics
and retains its separate-file-per-run semantics. It is not a stdout/stderr
logger and is not subject to this policy.

### 11.5 Implementation sequence

1. **Land `src/core/logging.{hpp,cpp}`** (§8.1) as a small, self-contained PR.
   Named-logger factory, rotating file sink, stderr mirror for levels ≥ WARN,
   env-var / CLI / TIL level control.
2. **Migrate the 41 `fprintf(stderr)` sites** (§2) subsystem by subsystem —
   one commit per subsystem keeps review manageable. After each migration,
   grep the subsystem for any remaining `fprintf(stderr`, `std::cerr`,
   `printf`, `std::cout` and reject any that are not bootstrap exceptions.
3. **Add a CI/lint check** that flags new `fprintf(stderr)` / `std::cerr` /
   `printf` / `std::cout` additions in diffs under `src/` and `include/`.
   Allowlist the handful of bootstrap sites by file:line. A `grep`-based
   pre-commit hook is sufficient; we do not need a proper linter rule.
4. **Extend EvolveLogger with `Decision`** (§8.5) as a follow-up for Phase 5a's
   selection-trace needs.

### 11.6 Enforcement

Going forward, any PR adding `fprintf(stderr)`, `std::cerr`, `printf`, or
`std::cout` outside the bootstrap exceptions listed in §11.2 is rejected at
review. The bootstrap exception list lives in this document — new sites
require a justification in the PR and an entry added here.

---

## 12. Scope expansion — ETIL I/O Channel Pipeline (cross-reference)

This survey covered *logging*. The broader need is a complete **I/O channel
pipeline** that subsumes logging as one case among many. The logging substrate
(spdlog + file sinks + named loggers) is the foundation for that pipeline, not
the ceiling.

### 12.1 Motivating scenarios the bare logger cannot handle

- **Redirect REPL `stdout` to a UDP sink** so a remote collector captures
  interpreter output without changing the REPL code or the TIL words.
- **Tee MCP server request traces to both a file and a live observable** that
  the TUI can subscribe to, so an operator can watch request flow in real time
  without tailing a file.
- **Attach a filter** that drops generation-level evolution noise but keeps
  fitness-improvement events and contribution-weight changes — composed at
  runtime, configured by the benchmark script, no recompile.
- **Annotate** every message on a channel with the current session ID and
  user ID without each producer knowing those exist.
- **Replay** a captured channel transcript into a test harness to reproduce
  a bug deterministically.

A raw spdlog file sink cannot satisfy any of these.

### 12.2 Shape of the design

A multi-source → multi-sink dataflow with composable transforms. Sources
produce messages on named channels; sinks consume from channels; transforms
sit between them (filter, annotate, batch, rate-limit, format). Routing is
loose-coupled in the Android-Intent style: producers do not know consumers,
and the router wires them by pattern match on channel names and tags.

Observables play two roles: the in-process transport for a channel (each
channel *is* an `Observable<Message>`), and the TIL-level control surface —
TIL words subscribe to channels, install transforms, and add routes at
runtime.

Packaged as a **separate static library** so REPL, MCP server, TUI client,
evolution benchmarks, and external tools all link against the same primitives.
Consumed via dependency injection (a `ChannelService` interface passed in at
construction) rather than global singletons, so tests can substitute a mock
and subsystems remain layer-independent.

### 12.3 Relationship between the two docs

- **`20260418A-Logging-Infrastructure-Survey.md`** (this doc) — Catalog current
  state; policy on direct stdio; migration plan to spdlog.
- **`20260418B-IO-Channel-Pipeline-Architecture.md`** — Architectural design
  for **Manifold**, the ETIL I/O channel pipeline library
  (`libetil-manifold`, namespace `etil::manifold`). Consumes the logging
  substrate.

The logging work in §8 / §11 is **not** superseded by §12. The spdlog-backed
`logging` module remains the terminal sink for diagnostic flows — Manifold
uses it as a back-end for the "durable file log" sink type. Do §8
first; it is the foundation and is independently valuable. Manifold
is the next layer up.

See `20260418B-IO-Channel-Pipeline-Architecture.md` for the Manifold
architecture sketch, API shapes, sink/transform inventory, observable
integration, RBAC model, broker-sink design, and message-identity scheme.