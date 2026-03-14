#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <streambuf>
#include <string>
#include <unordered_map>
#include <vector>

namespace etil::db { class MongoClient; }
namespace etil::aaa {
class UserStore;
class AuditLog;
}

namespace etil::mcp {

struct AuthConfig;

/// Maximum output size for MCP tool responses (10 MB).
constexpr size_t MCP_MAX_OUTPUT_SIZE = 10'485'760;

/// Maximum concurrent sessions.
constexpr size_t MAX_SESSIONS = 100;

/// Idle session timeout.
constexpr auto SESSION_IDLE_TIMEOUT = std::chrono::minutes(30);

/// Warning threshold — notify client when idle time exceeds this.
constexpr auto SESSION_IDLE_WARNING = std::chrono::minutes(25);

/// A string-backed streambuf that silently discards writes once a byte cap
/// is reached.  Avoids the RSS leak caused by std::ostringstream's
/// incremental buffer growth through glibc's brk() heap — large mmap'd
/// allocations from std::string ARE returned to the OS when freed.
class CappedStringBuf : public std::streambuf {
public:
    explicit CappedStringBuf(size_t cap = MCP_MAX_OUTPUT_SIZE)
        : cap_(cap) {}

    /// Extract the accumulated output and clear the buffer.
    /// The moved-from internal string is left empty (no heap allocation).
    std::string take() {
        std::string result = std::move(buf_);
        capped_ = false;
        return result;
    }

    /// Release all memory (swap with empty string guarantees deallocation).
    void reset() {
        std::string{}.swap(buf_);
        capped_ = false;
    }

    /// True if any writes were discarded due to the cap.
    bool was_capped() const { return capped_; }

    /// Current buffer size in bytes.
    size_t size() const { return buf_.size(); }

protected:
    int_type overflow(int_type ch) override {
        if (ch == traits_type::eof()) return traits_type::not_eof(ch);
        if (buf_.size() >= cap_) { capped_ = true; return ch; }
        buf_.push_back(static_cast<char>(ch));
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize count) override {
        if (buf_.size() >= cap_) { capped_ = true; return count; }
        size_t room = cap_ - buf_.size();
        size_t n = std::min(static_cast<size_t>(count), room);
        buf_.append(s, n);
        if (static_cast<size_t>(count) > n) capped_ = true;
        return count;  // always report success to avoid ostream error state
    }

private:
    std::string buf_;
    size_t cap_;
    bool capped_ = false;
};

/// MCP tool definition (name, description, input schema).
struct ToolDef {
    std::string name;
    std::string description;
    nlohmann::json input_schema;  // JSON Schema for arguments
};

/// MCP resource definition (URI, name, description, MIME type).
struct ResourceDef {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;
};

/// Per-session CPU and memory profiling counters.
///
/// All fields are plain uint64_t — MCP calls are mutex-serialized so no
/// atomics are needed.
struct SessionStats {
    uint64_t interpret_call_count = 0;
    uint64_t interpret_wall_ns = 0;
    uint64_t interpret_cpu_ns = 0;
    uint64_t current_rss_bytes = 0;
    uint64_t peak_rss_bytes = 0;
    uint64_t session_start_ms = 0;  // epoch milliseconds

    /// Zero all counters and set session_start_ms to now.
    void reset();

    /// Serialize to JSON with derived fields (Ms, Mb, uptime).
    nlohmann::json to_json(uint64_t concept_count, size_t stack_depth) const;

    /// Read current RSS from /proc/self/statm.  Returns 0 on failure.
    static uint64_t read_rss_bytes();

    /// Read process CPU time in nanoseconds via clock_gettime.
    static uint64_t cpu_time_ns();
};

// Forward declare Session (defined in session.hpp)
struct Session;

/// MCP Server for ETIL.
///
/// Manages multiple concurrent interpreter sessions.  Each session has
/// its own Dictionary, Interpreter, output buffers, and stats.
///
/// Two handle_message overloads:
/// - Single-arg (auto-session): creates a default session on first call.
///   Used by STDIO transport and unit tests.
/// - Two-arg (explicit session): routes to a specific session by ID.
///   Used by HTTP transport.
///
/// Transport-agnostic: call handle_message() directly for unit testing,
/// or wire it up to a Transport for production use.
class McpServer {
public:
    /// Tool handler: takes params JSON, returns result JSON.
    using ToolHandler = std::function<nlohmann::json(const nlohmann::json&)>;

    /// Resource handler: takes URI, returns content JSON.
    using ResourceHandler = std::function<nlohmann::json(const std::string&)>;

    McpServer();
    ~McpServer();

    // Non-copyable
    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    // --- Session lifecycle (used by HTTP transport) ---

    /// Create a new session.  Returns the session ID.
    /// Runs idle cleanup first; rejects if at MAX_SESSIONS after cleanup.
    /// user_id, role, and email are empty for API-key-only sessions.
    std::string create_session(const std::string& user_id = "",
                               const std::string& role = "",
                               const std::string& email = "");

    /// Destroy a session by ID.  No-op if the ID doesn't exist.
    void destroy_session(const std::string& session_id);

    /// Remove sessions that have been idle longer than SESSION_IDLE_TIMEOUT.
    void cleanup_idle_sessions();

    /// Check whether a session ID exists.
    bool has_session(const std::string& session_id) const;

    /// Number of active sessions.
    size_t session_count() const;

    // --- Message handling ---

    /// Process a JSON-RPC message for a specific session (HTTP transport).
    /// Returns nullopt for notifications (no response expected).
    /// Returns an error response if session_id is unknown.
    std::optional<nlohmann::json> handle_message(const std::string& session_id,
                                                  const nlohmann::json& msg);

    /// Process a JSON-RPC message using auto-session mode (STDIO, tests).
    /// Creates a default session on first call.
    std::optional<nlohmann::json> handle_message(const nlohmann::json& msg);

    /// Run the server on an HTTP transport with multi-session support.
    void run_http(class HttpTransport& transport);

    /// Access tool definitions (for testing).
    const std::vector<ToolDef>& tools() const { return tools_; }

    /// Access resource definitions (for testing).
    const std::vector<ResourceDef>& resources() const { return resources_; }

    /// Access the default session's interpreter (for testing).
    etil::core::Interpreter& interpreter();

    /// Generate a new UUID v4 session ID.
    static std::string generate_session_id();

private:
    // Protocol handlers
    nlohmann::json handle_initialize(const nlohmann::json& id,
                                     const nlohmann::json& params);
    nlohmann::json handle_tools_list(const nlohmann::json& id);
    nlohmann::json handle_tools_call(const nlohmann::json& id,
                                     const nlohmann::json& params);
    nlohmann::json handle_resources_list(const nlohmann::json& id);
    nlohmann::json handle_resources_read(const nlohmann::json& id,
                                         const nlohmann::json& params);

    // Tool registration
    void register_tool(std::string name, std::string description,
                       nlohmann::json input_schema, ToolHandler handler);
    void register_resource(std::string uri, std::string name,
                           std::string description, std::string mime_type,
                           ResourceHandler handler);

    void register_all_tools();
    void register_all_resources();

    // Constructor helpers — extract auth and database init for readability.
    void init_auth();
    void init_database();

    // Tool implementations
    nlohmann::json tool_interpret(const nlohmann::json& params);
    nlohmann::json tool_list_words(const nlohmann::json& params);
    nlohmann::json tool_get_word_info(const nlohmann::json& params);
    nlohmann::json tool_get_stack(const nlohmann::json& params);
    nlohmann::json tool_set_weight(const nlohmann::json& params);
    nlohmann::json tool_reset(const nlohmann::json& params);
    nlohmann::json tool_get_session_stats(const nlohmann::json& params);
    nlohmann::json tool_write_file(const nlohmann::json& params);
    nlohmann::json tool_list_files(const nlohmann::json& params);
    nlohmann::json tool_read_file(const nlohmann::json& params);
    nlohmann::json tool_list_sessions(const nlohmann::json& params);
    nlohmann::json tool_kick_session(const nlohmann::json& params);
#if defined(ETIL_HTTP_CLIENT_ENABLED) && defined(ETIL_JWT_ENABLED)
    nlohmann::json tool_manage_allowlist(const nlohmann::json& params);
#endif

#ifdef ETIL_JWT_ENABLED
    // Admin tools (role/user management)
    nlohmann::json tool_admin_list_roles(const nlohmann::json& params);
    nlohmann::json tool_admin_get_role(const nlohmann::json& params);
    nlohmann::json tool_admin_set_role(const nlohmann::json& params);
    nlohmann::json tool_admin_delete_role(const nlohmann::json& params);
    nlohmann::json tool_admin_list_users(const nlohmann::json& params);
    nlohmann::json tool_admin_set_user_role(const nlohmann::json& params);
    nlohmann::json tool_admin_delete_user(const nlohmann::json& params);
    nlohmann::json tool_admin_set_default_role(const nlohmann::json& params);
    nlohmann::json tool_admin_reload_config(const nlohmann::json& params);
#endif

    // Resource implementations
    nlohmann::json resource_dictionary(const std::string& uri);
    nlohmann::json resource_word(const std::string& uri);
    nlohmann::json resource_stack(const std::string& uri);
    nlohmann::json resource_session_stats(const std::string& uri);

    // Emit a real-time MCP notification to the client.
    void emit_notification(const std::string& msg);

    // Send a targeted notification to all sessions of a specific user.
    // Returns true if at least one session was found and notified.
    bool send_targeted_notification(const std::string& user_id,
                                    const std::string& msg);

    // Compute the effective idle timeout for a session.
    // Uses per-role config if available, else falls back to SESSION_IDLE_TIMEOUT.
    static std::chrono::steady_clock::duration effective_timeout(const Session& s);

    // Compute a warning threshold: max(timeout - 5min, timeout * 5/6),
    // floored at timeout / 2.
    static std::chrono::steady_clock::duration effective_warning(
        std::chrono::steady_clock::duration timeout);

    // Internal: dispatch a parsed request (current_session_ must be set).
    std::optional<nlohmann::json> dispatch_request(const nlohmann::json& msg);

    // Session map
    mutable std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::unique_ptr<Session>> sessions_;
    std::string default_session_id_;  // for auto-session mode

    // Volume configuration (from env vars, empty = not configured)
    std::string sessions_base_dir_;  // ETIL_SESSIONS_DIR
    std::string library_dir_;        // ETIL_LIBRARY_DIR

#ifdef ETIL_JWT_ENABLED
    // Auth configuration (loaded from ETIL_AUTH_CONFIG env var path).
    // shared_ptr<const> for thread-safe reload via atomic store/load.
    std::shared_ptr<const AuthConfig> auth_config_;
    std::string auth_config_dir_;  // directory path for persisting changes
    std::unique_ptr<class JwtAuth> jwt_auth_;
    // OAuth providers (keyed by provider name, e.g. "github", "google")
    std::unordered_map<std::string, std::unique_ptr<class OAuthProvider>>
        oauth_providers_;
#endif

#ifdef ETIL_MONGODB_ENABLED
    // Server-wide MongoDB client for user data (shared across all sessions)
    std::unique_ptr<class etil::db::MongoClient> mongo_client_;
    // Separate MongoDB client for AAA (different database, inaccessible to TIL primitives)
    std::unique_ptr<class etil::db::MongoClient> aaa_client_;
    // AAA layer uses aaa_client_ (not mongo_client_)
    std::unique_ptr<etil::aaa::UserStore> user_store_;
    std::unique_ptr<etil::aaa::AuditLog> audit_log_;
#endif

    // Thread-local session pointer set by handle_message() before dispatching.
    // Each HTTP worker thread gets its own copy, eliminating the data race
    // that occurred when multiple threads wrote/read a shared member
    // concurrently (one per session, all on different threads).
    static thread_local Session* current_session_;

    // Shared state
    class HttpTransport* transport_ = nullptr;

    // Registries (shared across all sessions)
    std::vector<ToolDef> tools_;
    std::vector<ToolHandler> tool_handlers_;
    std::vector<ResourceDef> resources_;
    std::vector<ResourceHandler> resource_handlers_;

    bool initialized_ = false;
};

} // namespace etil::mcp
