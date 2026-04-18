#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Central logging façade for ETIL.
///
/// Phase 0 of Manifold rollout — establishes the named-logger factory that
/// replaces direct stdout/stderr writes across the codebase. See
/// docs/claude-design/20260418A-Logging-Infrastructure-Survey.md for the
/// policy that forbids direct stdio logging, and
/// docs/claude-design/20260418C-Manifold-Phase-0-1-2-Implementation-Plan.md
/// for the phase 0 spec.
///
/// Dual backend (A-3 resolution):
///   Native: rotating file sink (main log file) + stderr sink (WARN+).
///   WASM:   stderr sink only (Emscripten routes stderr → console.error).
///
/// Public API is identical on both backends — callers receive a
/// std::shared_ptr<spdlog::logger> and invoke ->info / ->warn / ->error
/// / ->debug / ->trace / ->critical in the normal spdlog style.
///
/// Usage:
///     etil::core::logging::init("/var/log/etil", spdlog::level::info);
///     auto log = etil::core::logging::get("etil.mcp");
///     log->info("MCP server starting on {}:{}", host, port);
///     // ...
///     etil::core::logging::shutdown();

#include <memory>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/common.h>

namespace etil::core::logging {

/// Initialize the logging subsystem. Call once at process start, before any
/// `get()` call. Subsequent calls are no-ops (first-wins).
///
/// `log_dir` — directory for the rotating file sink. Must exist and be
///             writable. If empty or the directory is unwritable, the
///             file sink is omitted and only the stderr sink is active.
///             Ignored on WASM (no file sink).
/// `default_level` — level applied to all named loggers at creation.
///                   Can be overridden per-logger via `set_level`.
///                   Env var `ETIL_LOG_LEVEL` takes precedence if set.
///
/// The file sink, when active, writes to `<log_dir>/etil.log` with
/// rotation at 10 MB and 5 files retained.
void init(const std::string& log_dir,
          spdlog::level::level_enum default_level = spdlog::level::info);

/// Flush all loggers and tear down the subsystem. Call at process exit.
/// After this returns, `get()` creates loggers with the default sink
/// configuration (stderr only) as a safety net.
void shutdown();

/// Retrieve or create a named logger. Safe to call from any thread.
/// The first call for a given name creates the logger with the current
/// default level; subsequent calls return the same cached instance.
///
/// Canonical logger names used by the ETIL subsystems:
///   etil.mcp     — MCP server lifecycle / auth / errors
///   etil.http    — HTTP transport (request/response, server listen)
///   etil.db      — MongoDB client operations and errors
///   etil.aaa     — User store, audit log
///   etil.oauth   — OAuth device-code flows (GitHub, Google)
///   etil.session — MCP session pool (create, evict, expire)
///   etil.dict    — Dictionary registrations
///
/// Callers may use other names, but these are the recommended canonical
/// set and are recognized by operators reading logs.
std::shared_ptr<spdlog::logger> get(const std::string& name);

/// Set the level for a specific named logger. If the logger does not
/// exist yet, it will be created with the given level.
void set_level(const std::string& name, spdlog::level::level_enum level);

/// Set the level for all currently-registered loggers, and use as the
/// default for subsequently-created ones.
void set_all_levels(spdlog::level::level_enum level);

/// Parse a string like "trace", "debug", "info", "warn", "error",
/// "critical", or "off" into an spdlog level. Unknown strings return
/// `spdlog::level::info`.
spdlog::level::level_enum level_from_string(const std::string& s);

/// True if `init()` has run successfully. False if `init()` was never
/// called, or was called with an unusable log_dir. A false return does
/// not mean logging is broken — the stderr fallback is always available
/// via `get()`.
bool is_initialized();

}  // namespace etil::core::logging
