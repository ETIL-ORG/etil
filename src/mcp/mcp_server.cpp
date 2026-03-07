// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/mcp_server.hpp"
#include "etil/mcp/session.hpp"
#include "etil/mcp/json_rpc.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/version.hpp"

#include "etil/mcp/http_transport.hpp"

#ifdef ETIL_JWT_ENABLED
#include "etil/mcp/auth_config.hpp"
#include "etil/mcp/jwt_auth.hpp"
#include "etil/mcp/oauth_provider.hpp"
#include "etil/mcp/oauth_github.hpp"
#include "etil/mcp/oauth_google.hpp"
#endif

#ifdef ETIL_MONGODB_ENABLED
#include "etil/db/mongo_client.hpp"
#include "etil/aaa/user_store.hpp"
#include "etil/aaa/audit_log.hpp"
#endif

#include <cstdio>
#include <cstdlib>
#include <random>
#include <sstream>
#include <iomanip>

namespace etil::mcp {

// Static thread_local definition — each HTTP worker thread gets its own copy.
thread_local Session* McpServer::current_session_ = nullptr;

McpServer::McpServer() {
    // Read volume configuration from environment variables
    const char* sessions_env = std::getenv("ETIL_SESSIONS_DIR");
    if (sessions_env && sessions_env[0] != '\0') {
        sessions_base_dir_ = sessions_env;
    }
    const char* library_env = std::getenv("ETIL_LIBRARY_DIR");
    if (library_env && library_env[0] != '\0') {
        library_dir_ = library_env;
    }

#ifdef ETIL_JWT_ENABLED
    // Load auth configuration from ETIL_AUTH_CONFIG env var (directory path)
    const char* auth_config_env = std::getenv("ETIL_AUTH_CONFIG");
    if (auth_config_env && auth_config_env[0] != '\0') {
        try {
            auth_config_ = std::make_unique<AuthConfig>(
                AuthConfig::from_directory(auth_config_env));
            if (!auth_config_->jwt_private_key.empty() &&
                !auth_config_->jwt_public_key.empty()) {
                jwt_auth_ = std::make_unique<JwtAuth>(auth_config_.get());
                fprintf(stderr, "JWT authentication enabled (config: %s)\n",
                        auth_config_env);
            } else {
                fprintf(stderr, "Warning: auth config loaded but JWT keys "
                                "not configured\n");
            }
            // Create OAuth providers from config
            for (const auto& [name, prov_cfg] : auth_config_->providers) {
                if (!prov_cfg.enabled || prov_cfg.client_id.empty()) continue;

                if (name == "github") {
                    oauth_providers_[name] =
                        std::make_unique<GitHubProvider>(prov_cfg.client_id);
                    fprintf(stderr, "OAuth provider enabled: github\n");
                } else if (name == "google") {
                    if (prov_cfg.client_secret.empty()) {
                        fprintf(stderr,
                                "Warning: Google OAuth requires client_secret "
                                "(skipping)\n");
                        continue;
                    }
                    oauth_providers_[name] =
                        std::make_unique<GoogleProvider>(prov_cfg.client_id,
                                                        prov_cfg.client_secret);
                    fprintf(stderr, "OAuth provider enabled: google\n");
                } else {
                    fprintf(stderr, "Warning: unknown OAuth provider '%s' "
                                    "(skipping)\n", name.c_str());
                }
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "Warning: failed to load auth config '%s': %s\n",
                    auth_config_env, e.what());
        }
    }
#endif

#ifdef ETIL_MONGODB_ENABLED
    // Initialize MongoDB clients from connections config file + env overrides.
    // Two separate clients: one for user data (TIL primitives), one for AAA
    // (audit_log + users collections in a separate database, inaccessible to
    // TIL code).
    {
        auto mongo_cfg = etil::db::MongoConnectionsConfig::resolve();
        if (mongo_cfg.enabled()) {
            // User-data client (wired to TIL mongo-* primitives)
            mongo_client_ = std::make_unique<etil::db::MongoClient>();
            if (!mongo_client_->connect(mongo_cfg.uri, mongo_cfg.database)) {
                fprintf(stderr, "Warning: MongoDB connection failed\n");
                mongo_client_.reset();
            }

            // AAA client (separate database for audit_log + users)
            std::string aaa_db = "etil_aaa";
            if (const char* v = std::getenv("ETIL_MONGODB_AAA_DATABASE")) {
                if (v[0] != '\0') aaa_db = v;
            }
            aaa_client_ = std::make_unique<etil::db::MongoClient>();
            if (!aaa_client_->connect(mongo_cfg.uri, aaa_db)) {
                fprintf(stderr, "Warning: MongoDB AAA connection failed\n");
                aaa_client_.reset();
            }

            // Build AAA layer on the AAA client (not the user-data client)
            if (aaa_client_) {
                user_store_ = std::make_unique<etil::aaa::UserStore>(
                    *aaa_client_);
                audit_log_ = std::make_unique<etil::aaa::AuditLog>(
                    *aaa_client_);
                user_store_->ensure_indexes();
                audit_log_->ensure_indexes();
                fprintf(stderr, "AAA database: %s\n", aaa_db.c_str());
            }
        }
    }
#endif

    register_all_tools();
    register_all_resources();
}

McpServer::~McpServer() = default;

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

std::string McpServer::generate_session_id() {
    // UUID v4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(0, 15);
    std::uniform_int_distribution<uint32_t> dist_variant(8, 11);

    const char hex[] = "0123456789abcdef";
    std::string uuid(36, '-');
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue; // dash
        }
        if (i == 14) {
            uuid[i] = '4';
        } else if (i == 19) {
            uuid[i] = hex[dist_variant(rng)];
        } else {
            uuid[i] = hex[dist(rng)];
        }
    }
    return uuid;
}

std::string McpServer::create_session(const std::string& user_id,
                                      const std::string& role,
                                      const std::string& email) {
    // Pre-check capacity (brief lock).  Idle cleanup + size check only.
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = sessions_.begin(); it != sessions_.end(); ) {
            if (now - it->second->last_activity > SESSION_IDLE_TIMEOUT) {
                it = sessions_.erase(it);
            } else {
                ++it;
            }
        }
        if (sessions_.size() >= MAX_SESSIONS) {
            return {};  // empty string signals rejection
        }
    }

    // Construct outside the lock — register_primitives() + load_startup_files()
    // take hundreds of milliseconds per session.  Holding sessions_mutex_
    // during construction serialized all concurrent session creation.
    auto id = generate_session_id();
    auto session = std::make_unique<Session>(id, sessions_base_dir_, library_dir_);

#ifdef ETIL_JWT_ENABLED
    // Apply JWT identity and role permissions to the session
    if (!user_id.empty()) {
        session->user_id = user_id;
        session->email = email;

#ifdef ETIL_MONGODB_ENABLED
        // When MongoDB is active, resolve role dynamically from the database.
        // This means role changes in MongoDB take effect on next session creation
        // without waiting for JWT expiry.
        std::string effective_role = role;
        if (user_store_ && user_store_->available() && !email.empty()) {
            auto db_role = user_store_->get_role(email);
            if (!db_role.empty()) {
                effective_role = db_role;
            }
        }
        session->role = effective_role;
#else
        session->role = role;
#endif

        if (auth_config_) {
            auto* perms = auth_config_->permissions_for(user_id);
            if (perms) {
                session->apply_role_permissions(*perms);
            }
        }
    }
#else
    (void)user_id;
    (void)role;
    (void)email;
#endif

#ifdef ETIL_MONGODB_ENABLED
    // Wire MongoDB client state into the session
    if (mongo_client_ && mongo_client_->connected()) {
        session->mongo_state = std::make_unique<etil::db::MongoClientState>();
        session->mongo_state->client = mongo_client_.get();
#ifdef ETIL_JWT_ENABLED
        if (auth_config_ && !user_id.empty()) {
            auto* perms = auth_config_->permissions_for(user_id);
            if (perms) {
                session->mongo_state->query_quota = perms->mongo_query_quota;
            }
        }
#endif
        session->interp->context().set_mongo_client_state(
            session->mongo_state.get());
    }
#endif

    // Re-check capacity and insert (brief lock).
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        if (sessions_.size() >= MAX_SESSIONS) {
            return {};  // another thread filled up while we were constructing
        }
#ifdef ETIL_JWT_ENABLED
        // Per-user session quota (JWT-authenticated users only).
        // API-key sessions (empty user_id) skip this — bounded by MAX_SESSIONS.
        if (!user_id.empty() && auth_config_) {
            auto* perms = auth_config_->permissions_for(user_id);
            int max_user = perms ? perms->max_sessions : 2;
            int user_count = 0;
            for (const auto& [sid, s] : sessions_) {
                if (s->user_id == user_id) ++user_count;
            }
            if (user_count >= max_user) {
                return {};  // per-user quota exceeded
            }
        }
#endif
        sessions_.emplace(id, std::move(session));
    }

#ifdef ETIL_MONGODB_ENABLED
    if (audit_log_ && audit_log_->available()) {
        std::string audit_email;
#ifdef ETIL_JWT_ENABLED
        audit_email = email;
#endif
        audit_log_->log_session_create(audit_email, id, role);
    }
#endif

    return id;
}

void McpServer::destroy_session(const std::string& session_id) {
#ifdef ETIL_MONGODB_ENABLED
    // Log audit event before erasing (need session data)
    if (audit_log_ && audit_log_->available()) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            std::string audit_email;
#ifdef ETIL_JWT_ENABLED
            audit_email = it->second->email;
#endif
            audit_log_->log_session_destroy(audit_email, session_id);
        }
        sessions_.erase(session_id);
        return;
    }
#endif
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.erase(session_id);
}

void McpServer::cleanup_idle_sessions() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto now = std::chrono::steady_clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (now - it->second->last_activity > SESSION_IDLE_TIMEOUT) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

bool McpServer::has_session(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.count(session_id) > 0;
}

size_t McpServer::session_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.size();
}

// ---------------------------------------------------------------------------
// Message handling
// ---------------------------------------------------------------------------

std::optional<nlohmann::json> McpServer::handle_message(
    const std::string& session_id, const nlohmann::json& msg) {
    // Look up the session and check idle time before resetting last_activity
    Session* session = nullptr;
    std::chrono::steady_clock::duration idle_duration{};
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return make_error(nullptr, JsonRpcError::InvalidRequest,
                              "Unknown session: " + session_id);
        }
        session = it->second.get();
        auto now = std::chrono::steady_clock::now();
        idle_duration = now - session->last_activity;
        session->last_activity = now;
    }

    // Lock the session for the duration of request processing
    std::lock_guard<std::mutex> session_lock(session->mutex);
    current_session_ = session;

    // Warn if the session was approaching idle timeout
    if (idle_duration > SESSION_IDLE_WARNING) {
        auto remaining = std::chrono::duration_cast<std::chrono::minutes>(
            SESSION_IDLE_TIMEOUT - idle_duration);
        auto mins = remaining.count();
        if (mins > 0) {
            emit_notification("Session was idle — would have expired in "
                              + std::to_string(mins) + " minute"
                              + (mins == 1 ? "" : "s")
                              + ". Timer reset.");
        }
    }

    auto result = dispatch_request(msg);
    current_session_ = nullptr;
    return result;
}

std::optional<nlohmann::json> McpServer::handle_message(const nlohmann::json& msg) {
    // Auto-session mode: create a default session on first call
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        if (default_session_id_.empty()) {
            auto id = generate_session_id();
            sessions_.emplace(id, std::make_unique<Session>(
                id, sessions_base_dir_, library_dir_));
            default_session_id_ = id;
        }
    }

    return handle_message(default_session_id_, msg);
}

etil::core::Interpreter& McpServer::interpreter() {
    // Ensure the default session exists
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        if (default_session_id_.empty()) {
            auto id = generate_session_id();
            sessions_.emplace(id, std::make_unique<Session>(
                id, sessions_base_dir_, library_dir_));
            default_session_id_ = id;
        }
        return *sessions_.at(default_session_id_)->interp;
    }
}

std::optional<nlohmann::json> McpServer::dispatch_request(const nlohmann::json& msg) {
    try {
        nlohmann::json error_out;
        auto req = parse_request(msg, error_out);
        if (!req) {
            return error_out;
        }

        // Notifications (no id) don't get responses
        bool is_notification = !req->id.has_value();
        auto id = req->id.value_or(nullptr);

        // Route by method
        if (req->method == "initialize") {
            initialized_ = true;
            return handle_initialize(id, req->params);
        }

        if (req->method == "notifications/initialized") {
            return std::nullopt;
        }

        if (req->method == "tools/list") {
            return handle_tools_list(id);
        }

        if (req->method == "tools/call") {
            return handle_tools_call(id, req->params);
        }

        if (req->method == "resources/list") {
            return handle_resources_list(id);
        }

        if (req->method == "resources/read") {
            return handle_resources_read(id, req->params);
        }

        if (req->method == "ping") {
            return make_response(id, nlohmann::json::object());
        }

        // Unknown method
        if (is_notification) {
            return std::nullopt;
        }
        return make_error(id, JsonRpcError::MethodNotFound,
                          "Method not found: " + req->method);
    } catch (const std::exception& e) {
        fprintf(stderr, "MCP server error: %s\n", e.what());
        return make_error(nullptr, JsonRpcError::InternalError,
                          std::string("Server error: ") + e.what());
    } catch (...) {
        fprintf(stderr, "MCP server unknown error\n");
        return make_error(nullptr, JsonRpcError::InternalError,
                          "Unknown internal server error");
    }
}

void McpServer::run_http(HttpTransport& transport) {
    transport_ = &transport;

#ifdef ETIL_JWT_ENABLED
    // Wire auth config into the transport for JWT validation
    transport.config().jwt_auth = jwt_auth_.get();
    transport.config().auth_config = auth_config_.get();
    transport.config().oauth_providers = &oauth_providers_;
#endif

#ifdef ETIL_MONGODB_ENABLED
    transport.config().user_store = user_store_.get();
    transport.config().audit_log = audit_log_.get();
#endif

    SessionCallbacks callbacks;
    callbacks.create_session = [this](const std::string& user_id,
                                      const std::string& role,
                                      const std::string& email) {
        return create_session(user_id, role, email);
    };
    callbacks.destroy_session = [this](const std::string& id) { destroy_session(id); };
    callbacks.has_session = [this](const std::string& id) { return has_session(id); };

    transport.run(
        [this](const std::string& session_id,
               const nlohmann::json& msg) -> std::optional<nlohmann::json> {
            return handle_message(session_id, msg);
        },
        std::move(callbacks));

    transport_ = nullptr;
}

void McpServer::emit_notification(const std::string& msg) {
    if (!transport_) return;
    nlohmann::json notif = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/message"},
        {"params", {{"level", "info"}, {"data", msg}}}
    };
    transport_->send(notif);
}

bool McpServer::send_targeted_notification(const std::string& user_id,
                                            const std::string& msg) {
    if (!transport_) return false;

    // In auto-session mode (no JWT), user_id matching isn't meaningful.
    // We still iterate to support the primitive returning a useful flag.
    bool found = false;
    nlohmann::json notif = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/message"},
        {"params", {{"level", "info"}, {"data", msg},
                    {"logger", "user-notification"}}}
    };

    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (const auto& [sid, sess] : sessions_) {
#ifdef ETIL_JWT_ENABLED
        if (sess->user_id == user_id) {
            // Send via the transport (the session's client will receive it
            // via the shared transport's notification mechanism)
            transport_->send(notif);
            found = true;
        }
#else
        (void)user_id;
        (void)sid;
        (void)sess;
#endif
    }
    return found;
}

// ---------------------------------------------------------------------------
// Protocol handlers
// ---------------------------------------------------------------------------

nlohmann::json McpServer::handle_initialize(const nlohmann::json& id,
                                            const nlohmann::json& /*params*/) {
    nlohmann::json result = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {
            {"logging", nlohmann::json::object()},
            {"tools", {{"listChanged", false}}},
            {"resources", {{"subscribe", false}, {"listChanged", false}}}
        }},
        {"serverInfo", {
            {"name", "etil-mcp"},
            {"version", etil::core::ETIL_VERSION}
        }}
    };
    return make_response(id, result);
}

nlohmann::json McpServer::handle_tools_list(const nlohmann::json& id) {
    nlohmann::json tool_array = nlohmann::json::array();
    for (const auto& t : tools_) {
        tool_array.push_back({
            {"name", t.name},
            {"description", t.description},
            {"inputSchema", t.input_schema}
        });
    }
    return make_response(id, {{"tools", tool_array}});
}

nlohmann::json McpServer::handle_tools_call(const nlohmann::json& id,
                                            const nlohmann::json& params) {
    if (!params.contains("name") || !params["name"].is_string()) {
        return make_error(id, JsonRpcError::InvalidParams,
                          "Missing or invalid 'name' parameter");
    }

    std::string tool_name = params["name"].get<std::string>();
    nlohmann::json args = params.value("arguments", nlohmann::json::object());

    // Find the tool by name
    for (size_t i = 0; i < tools_.size(); ++i) {
        if (tools_[i].name == tool_name) {
            try {
                auto result = tool_handlers_[i](args);
                return make_response(id, result);
            } catch (const std::exception& e) {
                return make_error(id, JsonRpcError::InternalError,
                                  std::string("Tool error: ") + e.what());
            }
        }
    }

    return make_error(id, JsonRpcError::InvalidParams,
                      "Unknown tool: " + tool_name);
}

nlohmann::json McpServer::handle_resources_list(const nlohmann::json& id) {
    nlohmann::json res_array = nlohmann::json::array();
    for (const auto& r : resources_) {
        res_array.push_back({
            {"uri", r.uri},
            {"name", r.name},
            {"description", r.description},
            {"mimeType", r.mime_type}
        });
    }
    return make_response(id, {{"resources", res_array}});
}

nlohmann::json McpServer::handle_resources_read(const nlohmann::json& id,
                                                 const nlohmann::json& params) {
    if (!params.contains("uri") || !params["uri"].is_string()) {
        return make_error(id, JsonRpcError::InvalidParams,
                          "Missing or invalid 'uri' parameter");
    }

    std::string uri = params["uri"].get<std::string>();

    // Find the resource by URI (exact match or template match)
    for (size_t i = 0; i < resources_.size(); ++i) {
        const auto& r = resources_[i];
        // Check exact match
        if (r.uri == uri) {
            try {
                auto result = resource_handlers_[i](uri);
                return make_response(id, result);
            } catch (const std::exception& e) {
                return make_error(id, JsonRpcError::InternalError,
                                  std::string("Resource error: ") + e.what());
            }
        }
        // Check template match (e.g., "etil://word/{name}" matches "etil://word/dup")
        std::string tmpl = r.uri;
        auto brace_pos = tmpl.find('{');
        if (brace_pos != std::string::npos) {
            std::string prefix = tmpl.substr(0, brace_pos);
            if (uri.substr(0, prefix.size()) == prefix && uri.size() > prefix.size()) {
                try {
                    auto result = resource_handlers_[i](uri);
                    return make_response(id, result);
                } catch (const std::exception& e) {
                    return make_error(id, JsonRpcError::InternalError,
                                      std::string("Resource error: ") + e.what());
                }
            }
        }
    }

    return make_error(id, JsonRpcError::InvalidParams,
                      "Unknown resource: " + uri);
}

// ---------------------------------------------------------------------------
// Registration helpers
// ---------------------------------------------------------------------------

void McpServer::register_tool(std::string name, std::string description,
                               nlohmann::json input_schema, ToolHandler handler) {
    tools_.push_back({std::move(name), std::move(description), std::move(input_schema)});
    tool_handlers_.push_back(std::move(handler));
}

void McpServer::register_resource(std::string uri, std::string name,
                                   std::string description, std::string mime_type,
                                   ResourceHandler handler) {
    resources_.push_back({std::move(uri), std::move(name),
                          std::move(description), std::move(mime_type)});
    resource_handlers_.push_back(std::move(handler));
}

} // namespace etil::mcp
