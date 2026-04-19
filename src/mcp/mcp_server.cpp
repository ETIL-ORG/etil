// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/mcp_server.hpp"
#include "etil/mcp/session.hpp"
#include "etil/mcp/json_rpc.hpp"
#include "etil/mcp/mcp_sse_out_sink.hpp"
#include "etil/core/logging.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/version.hpp"
#include "etil/manifold/message.hpp"
#include "etil/manifold/service.hpp"

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

#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>
#include <typeindex>
#include <iomanip>

namespace etil::mcp {

namespace {
auto& log() {
    static auto logger = etil::core::logging::get("etil.mcp");
    return logger;
}
auto& session_log() {
    static auto logger = etil::core::logging::get("etil.session");
    return logger;
}
} // namespace

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

    // Manifold channel service — §17 Phase A outbound SSE. A route on
    // etil.mcp.out.notification.** is attached whose terminal sink
    // bridges back to the MCP transport via emit_notification /
    // send_targeted_notification. sys-notification and
    // user-notification TIL words publish onto these channels through
    // the notification_sender_ closures set in tool_handlers.cpp.
    channels_ = etil::manifold::make_default_channel_service();
    etil::manifold::RouteSpec sse_spec;
    sse_spec.channel_pattern = "etil.mcp.out.notification.**";
    sse_spec.sink = make_mcp_sse_out_sink(this);
    sse_out_route_handle_ = channels_->add_route(std::move(sse_spec));

    init_auth();
    init_database();
    register_all_tools();
    register_all_resources();
}

void McpServer::init_auth() {
#ifdef ETIL_JWT_ENABLED
    const char* auth_config_env = std::getenv("ETIL_AUTH_CONFIG");
    if (!auth_config_env || auth_config_env[0] == '\0') return;

    try {
        auth_config_dir_ = auth_config_env;
        auth_config_ = std::make_shared<const AuthConfig>(
            AuthConfig::from_directory(auth_config_env));
        if (!auth_config_->jwt_private_key.empty() &&
            !auth_config_->jwt_public_key.empty()) {
            jwt_auth_ = std::make_unique<JwtAuth>(
                auth_config_->jwt_private_key,
                auth_config_->jwt_public_key,
                auth_config_->jwt_ttl_seconds);
            log()->info("JWT authentication enabled (config: {})",
                        auth_config_env);
        } else {
            log()->warn("auth config loaded but JWT keys not configured");
        }
        // Create OAuth providers from config
        for (const auto& [name, prov_cfg] : auth_config_->providers) {
            if (!prov_cfg.enabled || prov_cfg.client_id.empty()) continue;

            if (name == "github") {
                oauth_providers_[name] =
                    std::make_unique<GitHubProvider>(prov_cfg.client_id);
                log()->info("OAuth provider enabled: github");
            } else if (name == "google") {
                if (prov_cfg.client_secret.empty()) {
                    log()->warn("Google OAuth requires client_secret (skipping)");
                    continue;
                }
                oauth_providers_[name] =
                    std::make_unique<GoogleProvider>(prov_cfg.client_id,
                                                    prov_cfg.client_secret);
                log()->info("OAuth provider enabled: google");
            } else {
                log()->warn("unknown OAuth provider '{}' (skipping)", name);
            }
        }
    } catch (const std::exception& e) {
        log()->warn("failed to load auth config '{}': {}",
                    auth_config_env, e.what());
    }
#endif
}

void McpServer::init_database() {
#ifdef ETIL_MONGODB_ENABLED
    auto mongo_cfg = etil::db::MongoConnectionsConfig::resolve();
    if (!mongo_cfg.enabled()) return;

    // User-data client (wired to TIL mongo-* primitives)
    mongo_client_ = std::make_unique<etil::db::MongoClient>();
    if (!mongo_client_->connect(mongo_cfg.uri, mongo_cfg.database)) {
        log()->warn("MongoDB connection failed");
        mongo_client_.reset();
    }

    // AAA client (separate database for audit_log + users)
    std::string aaa_db = "etil_aaa";
    if (const char* v = std::getenv("ETIL_MONGODB_AAA_DATABASE")) {
        if (v[0] != '\0') aaa_db = v;
    }
    aaa_client_ = std::make_unique<etil::db::MongoClient>();
    if (!aaa_client_->connect(mongo_cfg.uri, aaa_db)) {
        log()->warn("MongoDB AAA connection failed");
        aaa_client_.reset();
    }

    // Build AAA layer on the AAA client (not the user-data client)
    if (aaa_client_) {
        user_store_ = std::make_unique<etil::aaa::UserStore>(*aaa_client_);
        audit_log_ = std::make_unique<etil::aaa::AuditLog>(*aaa_client_);
        user_store_->ensure_indexes();
        audit_log_->ensure_indexes();
        log()->info("AAA database: {}", aaa_db);
    }
#endif
}

McpServer::~McpServer() {
    // Tear down the SSE out-route before the server goes away — the
    // sink holds a raw `this` pointer.
    if (channels_ && sse_out_route_handle_.valid()) {
        channels_->remove_route(sse_out_route_handle_);
    }
}

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
            if (now - it->second->last_activity >
                effective_timeout(*it->second)) {
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

        // Resolve role from file-based config (users.json).  This is the
        // authoritative source — it drives both session->role (displayed by
        // /whoami) and permission enforcement.  Falls back to JWT role if
        // the user has no file-based mapping.
        if (auth_config_) {
            session->role = auth_config_->role_for(user_id);
            auto* perms = auth_config_->permissions_for(user_id);
            if (perms) {
                session->apply_role_permissions(*perms);
            }
        } else {
            session->role = role;  // JWT role as fallback
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
        session->interp().context().set_mongo_client_state(
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
                // Evict the user's oldest session to make room.  This handles
                // leaked sessions (TUI crash, network drop, sandbox not cleaned
                // up) that would otherwise block the user until idle timeout.
                std::string oldest_sid;
                auto oldest_time = std::chrono::steady_clock::time_point::max();
                for (const auto& [sid, s] : sessions_) {
                    if (s->user_id == user_id &&
                        s->last_activity < oldest_time) {
                        oldest_time = s->last_activity;
                        oldest_sid = sid;
                    }
                }
                if (!oldest_sid.empty()) {
                    session_log()->info(
                        "Evicting oldest session {} for user {} "
                        "(per-user quota {})",
                        oldest_sid, user_id, max_user);
                    sessions_.erase(oldest_sid);
                } else {
                    return {};  // shouldn't happen, but fail safe
                }
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

    publish_session_event("opened", id, user_id);
    return id;
}

void McpServer::destroy_session(const std::string& session_id) {
    publish_session_event("closed", session_id);
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
        if (now - it->second->last_activity > effective_timeout(*it->second)) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

std::chrono::steady_clock::duration McpServer::effective_timeout(
    const Session& s) {
#ifdef ETIL_JWT_ENABLED
    if (s.permissions_ptr && s.permissions_ptr->session_idle_timeout_seconds > 0) {
        return std::chrono::seconds(
            s.permissions_ptr->session_idle_timeout_seconds);
    }
#else
    (void)s;
#endif
    return SESSION_IDLE_TIMEOUT;
}

std::chrono::steady_clock::duration McpServer::effective_warning(
    std::chrono::steady_clock::duration timeout) {
    using namespace std::chrono;
    auto five_min = minutes(5);
    auto minus_five = timeout - five_min;
    auto five_sixths = timeout * 5 / 6;
    auto half = timeout / 2;
    // max(timeout - 5min, timeout * 5/6), floored at timeout / 2
    auto result = std::max(minus_five, five_sixths);
    return std::max(result, half);
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
    // Phase 4b: stamp a start time and extract method for lifecycle
    // event emission. Method extraction is best-effort — malformed
    // messages get "<unknown>".
    const auto t_start = std::chrono::steady_clock::now();
    std::string method = "<unknown>";
    if (msg.is_object() && msg.contains("method") && msg["method"].is_string()) {
        method = msg["method"].get<std::string>();
    }
    publish_request_event("received", session_id, method, -1);

    auto finish_fail = [&](const std::string& err,
                           std::optional<nlohmann::json> r) {
        const auto t = std::chrono::steady_clock::now();
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            t - t_start).count();
        publish_request_event("failed", session_id, method, us, err);
        return r;
    };

    // Look up the session and check idle time before resetting last_activity
    Session* session = nullptr;
    std::chrono::steady_clock::duration idle_duration{};
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return finish_fail("unknown-session",
                               make_error(nullptr, JsonRpcError::InvalidRequest,
                                          "Unknown session: " + session_id));
        }
        session = it->second.get();
        auto now = std::chrono::steady_clock::now();
        idle_duration = now - session->last_activity;
        session->last_activity = now;
    }

    // Lock the session for request processing; scope block so we can
    // destroy the session after releasing the lock if force_terminate is set.
    bool should_terminate = false;
    std::optional<nlohmann::json> result;
    {
        std::lock_guard<std::mutex> session_lock(session->mutex);
        current_session_ = session;

        // Warn if the session was approaching idle timeout
        auto timeout = effective_timeout(*session);
        auto warning = effective_warning(timeout);
        if (idle_duration > warning) {
            auto remaining = std::chrono::duration_cast<std::chrono::minutes>(
                timeout - idle_duration);
            auto mins = remaining.count();
            if (mins > 0) {
                emit_notification("Session was idle — would have expired in "
                                  + std::to_string(mins) + " minute"
                                  + (mins == 1 ? "" : "s")
                                  + ". Timer reset.");
            }
        }

        result = dispatch_request(msg);
        current_session_ = nullptr;
        should_terminate = session->force_terminate;
    }

    if (should_terminate) {
        destroy_session(session_id);
    }

    // Phase 4b: emit completed/failed based on the response shape.
    const auto t_end = std::chrono::steady_clock::now();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        t_end - t_start).count();
    std::string err_msg;
    bool failed = false;
    if (result && result->is_object() && result->contains("error")) {
        failed = true;
        const auto& e = (*result)["error"];
        if (e.is_object() && e.contains("message") && e["message"].is_string()) {
            err_msg = e["message"].get<std::string>();
        } else {
            err_msg = "jsonrpc-error";
        }
    }
    publish_request_event(failed ? "failed" : "completed",
                           session_id, method, us, err_msg);

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
        return sessions_.at(default_session_id_)->interp();
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
            publish_inbound_notification(req->method, req->params);
            return std::nullopt;
        }

        // §17 Phase B — route inbound client notifications onto
        // etil.mcp.in.** channels. notifications/progress,
        // notifications/cancelled, notifications/roots/list_changed,
        // and arbitrary notifications/* become channel messages
        // tagged with session_id. No response; clients MUST NOT expect
        // one per JSON-RPC.
        if (is_notification && req->method.rfind("notifications/", 0) == 0) {
            publish_inbound_notification(req->method, req->params);
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
        log()->error("MCP server error: {}", e.what());
        return make_error(nullptr, JsonRpcError::InternalError,
                          std::string("Server error: ") + e.what());
    } catch (...) {
        log()->error("MCP server unknown error");
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
    callbacks.channels = channels_.get();

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

void McpServer::publish_inbound_notification(
    const std::string& method, const nlohmann::json& params) {
    if (!channels_) return;

    // Derive channel from method.
    std::string channel;
    if (method == "notifications/progress") {
        channel = "etil.mcp.in.progress";
    } else if (method == "notifications/cancelled") {
        channel = "etil.mcp.in.cancelled";
    } else if (method == "notifications/roots/list_changed") {
        channel = "etil.mcp.in.roots.changed";
    } else if (method == "notifications/initialized") {
        channel = "etil.mcp.in.initialized";
    } else if (method.rfind("notifications/", 0) == 0) {
        std::string tail = method.substr(std::strlen("notifications/"));
        // Replace '/' with '.' so the MCP method tree maps cleanly onto
        // the channel dotted-namespace grammar.
        for (auto& c : tail) if (c == '/') c = '.';
        channel = "etil.mcp.in.notification." + tail;
    } else {
        return;  // not a notifications/* method
    }

    etil::manifold::Message m;
    m.channel = std::move(channel);
    m.payload = params.is_null() ? std::string() : params.dump();
    m.payload_type = std::type_index(typeid(std::string));
    m.tags["method"] = method;
    if (current_session_ != nullptr) {
        m.tags["session_id"] = current_session_->id;
    }
    channels_->publish(std::move(m));
}

void McpServer::publish_request_event(const std::string& stage,
                                       const std::string& session_id,
                                       const std::string& method,
                                       int64_t latency_us,
                                       const std::string& error) {
    if (!channels_) return;
    etil::manifold::Message m;
    if (stage == "received")       m.channel = "etil.mcp.request.received";
    else if (stage == "completed") m.channel = "etil.mcp.request.completed";
    else if (stage == "failed")    m.channel = "etil.mcp.request.failed";
    else return;

    m.payload = std::string{};  // structured data lives in tags
    m.payload_type = std::type_index(typeid(std::string));
    if (!method.empty())    m.tags["method"] = method;
    if (!session_id.empty()) m.tags["session_id"] = session_id;
    if (latency_us >= 0)    m.tags["latency_us"] = std::to_string(latency_us);
    if (!error.empty())     m.tags["error"] = error;
    channels_->publish(std::move(m));
}

void McpServer::publish_session_event(const std::string& stage,
                                       const std::string& session_id,
                                       const std::string& user_id) {
    if (!channels_) return;
    etil::manifold::Message m;
    if (stage == "opened")      m.channel = "etil.mcp.session.opened";
    else if (stage == "closed") m.channel = "etil.mcp.session.closed";
    else return;
    m.payload = std::string{};
    m.payload_type = std::type_index(typeid(std::string));
    if (!session_id.empty()) m.tags["session_id"] = session_id;
    if (!user_id.empty())    m.tags["user_id"] = user_id;
    channels_->publish(std::move(m));
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

#ifdef ETIL_JWT_ENABLED
    // Include the server-resolved role so the client can display the
    // authoritative role (from users.json) instead of the JWT-baked role.
    if (current_session_ && !current_session_->role.empty()) {
        result["_meta"] = {{"role", current_session_->role}};
    }
#endif

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

    // Cap URI length to prevent abuse (1 KB)
    if (uri.size() > 1024) {
        return make_error(id, JsonRpcError::InvalidParams,
                          "Resource URI too long (max 1024 bytes)");
    }

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
