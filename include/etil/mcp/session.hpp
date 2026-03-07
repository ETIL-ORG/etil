#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/mcp/mcp_server.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>

namespace etil::core {
class Dictionary;
class Interpreter;
}

namespace etil::lvfs {
class Lvfs;
}

namespace etil::fileio {
class UvSession;
}

namespace etil::net {
struct HttpClientConfig;
struct HttpClientState;
}

namespace etil::db {
struct MongoClientConfig;
struct MongoClientState;
class MongoClient;
}

namespace etil::mcp {

/// Per-session interpreter state for multi-session MCP server.
///
/// Each Session owns its own Dictionary, Interpreter, output buffers,
/// and profiling stats.  Sessions are independent — different sessions
/// can execute concurrently on different threads.  The per-session mutex
/// serializes requests within a single session (Interpreter is not
/// thread-safe).
struct Session {
    std::string id;
    std::string home_dir;  // Actual filesystem path, empty if no volume
    std::unique_ptr<etil::core::Dictionary> dict;
    CappedStringBuf out_buf;
    CappedStringBuf err_buf;
    std::ostream interp_out{&out_buf};
    std::ostream interp_err{&err_buf};
    std::unique_ptr<etil::core::Interpreter> interp;
    std::unique_ptr<etil::lvfs::Lvfs> lvfs;
    std::unique_ptr<etil::fileio::UvSession> uv_session;
#ifdef ETIL_HTTP_CLIENT_ENABLED
    std::unique_ptr<etil::net::HttpClientState> http_state;
#endif
#ifdef ETIL_MONGODB_ENABLED
    std::unique_ptr<etil::db::MongoClientState> mongo_state;
#endif
#ifdef ETIL_JWT_ENABLED
    std::string user_id;   // "github:12345" — empty for API-key-only sessions
    std::string role;      // "admin" — empty for API-key-only sessions
    std::string email;     // "alice@example.com" — from JWT or OAuth user info
    const struct RolePermissions* permissions_ptr = nullptr;
#if defined(ETIL_HTTP_CLIENT_ENABLED)
    /// Owns the role-specific HttpClientConfig (if role overrides domains).
    std::unique_ptr<etil::net::HttpClientConfig> role_http_config;
#endif

    /// Apply role-based HTTP allowlist to session's HttpClientState.
    /// Called by McpServer after session construction when JWT auth is active.
    void apply_role_permissions(const struct RolePermissions& perms);
#endif
    SessionStats stats;
    std::mutex mutex;  // Serializes requests to this session
    std::chrono::steady_clock::time_point last_activity;

    explicit Session(const std::string& session_id,
                     const std::string& sessions_base_dir = "",
                     const std::string& library_dir = "");
    ~Session();

    // Non-copyable, non-movable (owns mutex and ostreams)
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
};

} // namespace etil::mcp
