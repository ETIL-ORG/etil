#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declare httplib types to avoid header leak
namespace httplib {
class Server;
}

namespace etil::aaa {
class UserStore;
class AuditLog;
}

namespace etil::mcp {

// Forward declare McpServer for session-aware handler
class McpServer;

/// Handler type: takes a JSON message, returns a response (or nullopt for notifications).
using MessageHandler = std::function<std::optional<nlohmann::json>(const nlohmann::json&)>;

/// Configuration for the HTTP transport.
struct HttpTransportConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    std::string api_key;                          // Empty = no auth required
    std::vector<std::string> allowed_origins;     // Empty = allow all
#ifdef ETIL_JWT_ENABLED
    const class JwtAuth* jwt_auth = nullptr;      // Non-owning; null = JWT disabled
    const struct AuthConfig* auth_config = nullptr;  // Non-owning; null = no config
    const std::unordered_map<std::string, std::unique_ptr<class OAuthProvider>>*
        oauth_providers = nullptr;  // Non-owning; null = no OAuth
#endif
#ifdef ETIL_MONGODB_ENABLED
    etil::aaa::UserStore* user_store = nullptr;   // Non-owning; null = no user mgmt
    etil::aaa::AuditLog* audit_log = nullptr;     // Non-owning; null = no audit
#endif
};

/// Session-aware message handler: takes session ID + JSON message.
using SessionMessageHandler =
    std::function<std::optional<nlohmann::json>(const std::string& session_id,
                                                 const nlohmann::json& msg)>;

/// Session lifecycle callbacks from the transport to the server.
struct SessionCallbacks {
    /// Create a new session.  user_id, role, and email are empty for API-key-only auth.
    std::function<std::string(const std::string& user_id,
                              const std::string& role,
                              const std::string& email)> create_session;
    std::function<void(const std::string&)> destroy_session;
    std::function<bool(const std::string&)> has_session;
};

/// HTTP transport for MCP JSON-RPC (Streamable HTTP).
///
/// Implements the MCP Streamable HTTP transport specification:
/// - POST /mcp: accepts JSON-RPC, returns application/json or text/event-stream
/// - GET /mcp: returns 405 (no long-lived SSE streaming yet)
/// - DELETE /mcp: terminates session
///
/// When notifications are emitted during request processing (via send()),
/// the POST response uses SSE (text/event-stream) with notification events
/// followed by the response event.  When no notifications are present,
/// the response is plain application/json (backward compatible).
///
/// Session lifecycle is delegated to McpServer.  The transport handles
/// authentication, parsing, and HTTP-level concerns only.
class HttpTransport {
public:
    explicit HttpTransport(HttpTransportConfig config);
    ~HttpTransport();

    void send(const nlohmann::json& message);
    void run(MessageHandler handler);
    void shutdown();

    /// Run with session-aware handler and callbacks.
    /// This is the preferred entry point for multi-session HTTP operation.
    void run(SessionMessageHandler handler, SessionCallbacks callbacks);

    /// Access the mutable config (for wiring auth at startup).
    HttpTransportConfig& config() { return config_; }

    // Helper methods (public for unit testing)

    /// Validate the Origin header against allowed origins list.
    /// Returns true if the origin is allowed (or if no restrictions set).
    bool validate_origin(const std::string& origin) const;

    /// Validate the Authorization header value against the configured API key.
    /// Returns true if the key matches (or if no API key configured).
    bool validate_api_key(const std::string& auth_header) const;

#ifdef ETIL_JWT_ENABLED
    /// Try to validate a JWT from an Authorization: Bearer header.
    /// Returns true and sets out_user_id/out_role/out_email on success.
    /// Returns false if the token is missing, invalid, or JWT is not configured.
    bool try_jwt_auth(const std::string& auth_header,
                      std::string& out_user_id,
                      std::string& out_role,
                      std::string& out_email) const;
#endif

    /// Build an SSE response body from buffered notifications + final response.
    /// Public for unit testing.
    static std::string build_sse_body(const std::vector<nlohmann::json>& notifications,
                                      const nlohmann::json& response);

    /// Clear any buffered notifications (call before request processing).
    static void clear_pending_notifications();

    /// Drain and return all buffered notifications (call after request processing).
    static std::vector<nlohmann::json> drain_pending_notifications();

private:
    HttpTransportConfig config_;
    std::unique_ptr<httplib::Server> server_;
    MessageHandler handler_;              // legacy single-session handler
    SessionMessageHandler session_handler_;
    SessionCallbacks session_callbacks_;

    void setup_routes();
    void setup_session_routes();

    /// Dispatch a message to the legacy handler with catch-all protection.
    std::optional<nlohmann::json> dispatch(const nlohmann::json& msg);

    /// Thread-local notification buffer (per-request, drained in POST handler).
    static thread_local std::vector<nlohmann::json> pending_notifications_;
};

} // namespace etil::mcp
