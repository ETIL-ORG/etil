// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/http_transport.hpp"
#include "etil/mcp/json_rpc.hpp"

#ifdef ETIL_JWT_ENABLED
#include "etil/mcp/auth_config.hpp"
#include "etil/mcp/jwt_auth.hpp"
#include "etil/mcp/oauth_provider.hpp"
#endif

#ifdef ETIL_MONGODB_ENABLED
#include "etil/aaa/user_store.hpp"
#include "etil/aaa/audit_log.hpp"
#endif

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace etil::mcp {

// Thread-local notification buffer — one per request thread.
thread_local std::vector<nlohmann::json> HttpTransport::pending_notifications_;

// Thread-local pointer to the active chunked response sink.
// When non-null, send() writes SSE events directly to the sink for real-time
// streaming instead of buffering into pending_notifications_.
namespace {
thread_local httplib::DataSink* active_sink = nullptr;
}

HttpTransport::HttpTransport(HttpTransportConfig config)
    : config_(std::move(config))
    , server_(std::make_unique<httplib::Server>()) {}

HttpTransport::~HttpTransport() {
    shutdown();
}

void HttpTransport::send(const nlohmann::json& message) {
    if (active_sink) {
        // Streaming mode: write SSE event directly to the response sink.
        std::string event = "data: " + message.dump() + "\n\n";
        active_sink->write(event.c_str(), event.size());
    } else {
        // Fallback: buffer for batch response (non-streaming callers).
        pending_notifications_.push_back(message);
    }
}

void HttpTransport::clear_pending_notifications() {
    pending_notifications_.clear();
}

std::vector<nlohmann::json> HttpTransport::drain_pending_notifications() {
    std::vector<nlohmann::json> result;
    result.swap(pending_notifications_);
    return result;
}

std::string HttpTransport::build_sse_body(
    const std::vector<nlohmann::json>& notifications,
    const nlohmann::json& response) {
    std::string body;
    for (const auto& notif : notifications) {
        body += "data: ";
        body += notif.dump();
        body += "\n\n";
    }
    body += "data: ";
    body += response.dump();
    body += "\n\n";
    return body;
}

void HttpTransport::run(MessageHandler handler) {
    handler_ = std::move(handler);
    setup_routes();

    fprintf(stderr, "ETIL MCP server listening on %s:%d\n",
            config_.host.c_str(), config_.port);

    server_->listen(config_.host, config_.port);
}

void HttpTransport::run(SessionMessageHandler handler, SessionCallbacks callbacks) {
    session_handler_ = std::move(handler);
    session_callbacks_ = std::move(callbacks);
    setup_session_routes();

    fprintf(stderr, "ETIL MCP server listening on %s:%d (multi-session)\n",
            config_.host.c_str(), config_.port);

    server_->listen(config_.host, config_.port);
}

void HttpTransport::shutdown() {
    if (server_) {
        server_->stop();
    }
}

// ---------------------------------------------------------------------------
// Legacy route setup (single-session, for STDIO compatibility)
// ---------------------------------------------------------------------------

void HttpTransport::setup_routes() {
    // POST /mcp — main JSON-RPC endpoint (legacy single-session)
    server_->Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Expose-Headers", "Mcp-Session-Id");

        if (!config_.allowed_origins.empty()) {
            auto origin = req.get_header_value("Origin");
            if (!origin.empty() && !validate_origin(origin)) {
                res.status = 403;
                res.set_content(R"({"error":"Forbidden: origin not allowed"})",
                                "application/json");
                return;
            }
        }

        if (!config_.api_key.empty()) {
            auto auth = req.get_header_value("Authorization");
            if (!validate_api_key(auth)) {
                res.status = 401;
                res.set_content(R"({"error":"Unauthorized: invalid or missing API key"})",
                                "application/json");
                return;
            }
        }

        nlohmann::json msg;
        try {
            msg = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::parse_error& e) {
            auto err = make_error(nullptr, JsonRpcError::ParseError,
                                  std::string("Parse error: ") + e.what());
            res.status = 200;
            res.set_content(err.dump(), "application/json");
            return;
        }

        bool is_notification = msg.is_object() && !msg.contains("id");

        if (is_notification) {
            // Notifications: run handler synchronously, return 202
            clear_pending_notifications();
            dispatch(msg);
            res.status = 202;
        } else {
            // Requests: stream SSE events via chunked content provider
            res.set_header("Cache-Control", "no-cache");
            res.set_header("X-Accel-Buffering", "no");

            nlohmann::json msg_copy = std::move(msg);

            res.set_chunked_content_provider(
                "text/event-stream",
                [this, msg_copy = std::move(msg_copy)]
                (size_t /*offset*/, httplib::DataSink &sink) -> bool {
                    active_sink = &sink;
                    clear_pending_notifications();
                    auto response = dispatch(msg_copy);
                    active_sink = nullptr;

                    if (response.has_value()) {
                        std::string event = "data: " + response->dump() + "\n\n";
                        sink.write(event.c_str(), event.size());
                    }
                    sink.done();
                    return true;
                }
            );
        }
    });

    // GET /mcp — SSE endpoint (not yet implemented)
    server_->Get("/mcp", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.status = 405;
        res.set_header("Allow", "POST, DELETE");
        res.set_content(R"({"error":"Method Not Allowed: SSE not yet supported"})",
                        "application/json");
    });

    // OPTIONS /mcp — CORS preflight
    server_->Options("/mcp", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers",
                        "Content-Type, Authorization, Mcp-Session-Id, Accept");
        res.set_header("Access-Control-Max-Age", "86400");
        res.status = 204;
    });
}

// ---------------------------------------------------------------------------
// Multi-session route setup
// ---------------------------------------------------------------------------

void HttpTransport::setup_session_routes() {
    // POST /mcp — main JSON-RPC endpoint (multi-session)
    server_->Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Expose-Headers", "Mcp-Session-Id");

        // Origin validation
        if (!config_.allowed_origins.empty()) {
            auto origin = req.get_header_value("Origin");
            if (!origin.empty() && !validate_origin(origin)) {
                res.status = 403;
                res.set_content(R"({"error":"Forbidden: origin not allowed"})",
                                "application/json");
                return;
            }
        }

        // Dual-mode authentication: JWT preferred, API key fallback
        auto auth_header = req.get_header_value("Authorization");
        std::string jwt_user_id;
        std::string jwt_role;
        std::string jwt_email;
        bool authenticated = false;

#ifdef ETIL_JWT_ENABLED
        // Try JWT first
        if (!authenticated && config_.jwt_auth) {
            if (try_jwt_auth(auth_header, jwt_user_id, jwt_role, jwt_email)) {
                authenticated = true;
            }
        }
#endif

        // Fall back to API key
        if (!authenticated) {
            if (!config_.api_key.empty()) {
                if (validate_api_key(auth_header)) {
                    authenticated = true;
                } else {
                    res.status = 401;
                    res.set_content(R"({"error":"Unauthorized: invalid or missing credentials"})",
                                    "application/json");
                    return;
                }
            } else {
                // No auth configured — allow all
                authenticated = true;
            }
        }

        // Parse JSON body
        nlohmann::json msg;
        try {
            msg = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::parse_error& e) {
            auto err = make_error(nullptr, JsonRpcError::ParseError,
                                  std::string("Parse error: ") + e.what());
            res.status = 200;
            res.set_content(err.dump(), "application/json");
            return;
        }

        bool is_initialize = msg.is_object() && msg.contains("method") &&
                             msg["method"].is_string() &&
                             msg["method"].get<std::string>() == "initialize";
        bool is_notification = msg.is_object() && !msg.contains("id");

        if (is_initialize) {
            // Create a new session via the server (pass JWT identity if present)
            std::string new_session = session_callbacks_.create_session(
                jwt_user_id, jwt_role, jwt_email);
            if (new_session.empty()) {
                res.status = 503;
                res.set_content(R"({"error":"Service Unavailable: max sessions reached"})",
                                "application/json");
                return;
            }

            // Stream SSE events via chunked content provider
            res.set_header("Mcp-Session-Id", new_session);
            res.set_header("Cache-Control", "no-cache");
            res.set_header("X-Accel-Buffering", "no");

            nlohmann::json msg_copy = std::move(msg);

            res.set_chunked_content_provider(
                "text/event-stream",
                [this, msg_copy = std::move(msg_copy),
                 new_session = std::move(new_session)]
                (size_t /*offset*/, httplib::DataSink &sink) -> bool {
                    active_sink = &sink;
                    clear_pending_notifications();
                    auto response = session_handler_(new_session, msg_copy);
                    active_sink = nullptr;

                    if (response.has_value()) {
                        std::string event = "data: " + response->dump() + "\n\n";
                        sink.write(event.c_str(), event.size());
                    }
                    sink.done();
                    return true;
                }
            );
            return;
        }

        // Non-initialize requests require a valid session ID
        auto client_session = req.get_header_value("Mcp-Session-Id");
        if (client_session.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"Bad Request: missing Mcp-Session-Id header"})",
                            "application/json");
            return;
        }

        if (!session_callbacks_.has_session(client_session)) {
            res.status = 404;
            res.set_content(R"({"error":"Not Found: invalid session ID"})",
                            "application/json");
            return;
        }

        if (is_notification) {
            // Notifications: run handler synchronously, return 202
            clear_pending_notifications();
            session_handler_(client_session, msg);
            res.status = 202;
        } else {
            // Requests: stream SSE events via chunked content provider
            res.set_header("Cache-Control", "no-cache");
            res.set_header("X-Accel-Buffering", "no");

            nlohmann::json msg_copy = std::move(msg);
            std::string session_copy = std::move(client_session);

            res.set_chunked_content_provider(
                "text/event-stream",
                [this, msg_copy = std::move(msg_copy),
                 session_copy = std::move(session_copy)]
                (size_t /*offset*/, httplib::DataSink &sink) -> bool {
                    active_sink = &sink;
                    clear_pending_notifications();
                    auto response = session_handler_(session_copy, msg_copy);
                    active_sink = nullptr;

                    if (response.has_value()) {
                        std::string event = "data: " + response->dump() + "\n\n";
                        sink.write(event.c_str(), event.size());
                    }
                    sink.done();
                    return true;
                }
            );
        }
    });

    // GET /mcp — SSE endpoint (not yet implemented)
    server_->Get("/mcp", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.status = 405;
        res.set_header("Allow", "POST, DELETE");
        res.set_content(R"({"error":"Method Not Allowed: SSE not yet supported"})",
                        "application/json");
    });

    // DELETE /mcp — terminate session
    server_->Delete("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");

        if (!config_.api_key.empty()) {
            auto auth = req.get_header_value("Authorization");
            if (!validate_api_key(auth)) {
                res.status = 401;
                res.set_content(R"({"error":"Unauthorized: invalid or missing API key"})",
                                "application/json");
                return;
            }
        }

        auto client_session = req.get_header_value("Mcp-Session-Id");
        if (client_session.empty() ||
            !session_callbacks_.has_session(client_session)) {
            res.status = 404;
            res.set_content(R"({"error":"Not Found: invalid session ID"})",
                            "application/json");
            return;
        }

        session_callbacks_.destroy_session(client_session);

        res.status = 200;
        res.set_content(R"({"status":"session terminated"})", "application/json");
    });

#ifdef ETIL_JWT_ENABLED
    if (config_.jwt_auth && config_.auth_config) {
        // POST /auth/device — initiate OAuth device flow.
        //
        // Request:  { "provider": "github" }
        // Response: { "device_code":"...", "user_code":"WDJB-MJHT",
        //             "verification_uri":"https://github.com/login/device",
        //             "expires_in":900, "interval":5 }
        server_->Post("/auth/device", [this](const httplib::Request& req,
                                              httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");

            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body);
            } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})",
                                "application/json");
                return;
            }

            if (!body.contains("provider") || !body["provider"].is_string()) {
                res.status = 400;
                res.set_content(
                    R"({"error":"Missing required field: provider"})",
                    "application/json");
                return;
            }

            std::string provider_name = body["provider"].get<std::string>();

            if (!config_.oauth_providers) {
                res.status = 400;
                res.set_content(
                    R"({"error":"No OAuth providers configured"})",
                    "application/json");
                return;
            }

            auto it = config_.oauth_providers->find(provider_name);
            if (it == config_.oauth_providers->end()) {
                res.status = 400;
                res.set_content(
                    R"({"error":"Unknown provider: )" + provider_name + "\"}",
                    "application/json");
                return;
            }

            auto device_resp = it->second->request_device_code();
            if (!device_resp) {
                res.status = 502;
                res.set_content(
                    R"({"error":"Provider unavailable"})",
                    "application/json");
                return;
            }

            nlohmann::json response = {
                {"device_code", device_resp->device_code},
                {"user_code", device_resp->user_code},
                {"verification_uri", device_resp->verification_uri},
                {"expires_in", device_resp->expires_in},
                {"interval", device_resp->interval}
            };
            res.set_content(response.dump(), "application/json");
        });

        // POST /auth/poll — poll device code status.
        //
        // Request:  { "provider": "github", "device_code": "..." }
        // Response (pending):  { "status": "pending" }
        // Response (granted):  { "token":"eyJ...", "role":"researcher",
        //                        "expires_in":86400 }
        // Response (error):    { "status":"expired_token", "error":"..." }
        server_->Post("/auth/poll", [this](const httplib::Request& req,
                                            httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");

            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body);
            } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})",
                                "application/json");
                return;
            }

            if (!body.contains("provider") || !body["provider"].is_string() ||
                !body.contains("device_code") ||
                !body["device_code"].is_string()) {
                res.status = 400;
                res.set_content(
                    R"({"error":"Missing required fields: provider, device_code"})",
                    "application/json");
                return;
            }

            std::string provider_name = body["provider"].get<std::string>();
            std::string device_code = body["device_code"].get<std::string>();

            if (!config_.oauth_providers) {
                res.status = 400;
                res.set_content(
                    R"({"error":"No OAuth providers configured"})",
                    "application/json");
                return;
            }

            auto it = config_.oauth_providers->find(provider_name);
            if (it == config_.oauth_providers->end()) {
                res.status = 400;
                res.set_content(
                    R"({"error":"Unknown provider: )" + provider_name + "\"}",
                    "application/json");
                return;
            }

            auto poll_result = it->second->poll_device_code(device_code);
            if (!poll_result) {
                res.status = 502;
                res.set_content(
                    R"({"error":"Provider unavailable"})",
                    "application/json");
                return;
            }

            switch (poll_result->status) {
            case PollResult::Status::Pending:
                res.set_content(R"({"status":"pending"})",
                                "application/json");
                return;

            case PollResult::Status::SlowDown: {
                nlohmann::json resp = {
                    {"status", "slow_down"},
                    {"interval", poll_result->interval}
                };
                res.set_content(resp.dump(), "application/json");
                return;
            }

            case PollResult::Status::ExpiredToken: {
                nlohmann::json resp = {
                    {"status", "expired_token"},
                    {"error", poll_result->error_description}
                };
                res.set_content(resp.dump(), "application/json");
                return;
            }

            case PollResult::Status::AccessDenied: {
                nlohmann::json resp = {
                    {"status", "access_denied"},
                    {"error", poll_result->error_description}
                };
                res.set_content(resp.dump(), "application/json");
                return;
            }

            case PollResult::Status::Error: {
                nlohmann::json resp = {
                    {"status", "error"},
                    {"error", poll_result->error_description}
                };
                res.status = 502;
                res.set_content(resp.dump(), "application/json");
                return;
            }

            case PollResult::Status::Granted:
                break;  // fall through to token minting
            }

            // Granted: validate via user info and mint ETIL JWT
            auto user_info = it->second->get_user_info(
                poll_result->access_token);
            if (!user_info) {
                res.status = 401;
                res.set_content(
                    R"({"error":"Failed to validate provider token"})",
                    "application/json");
                return;
            }

            std::string user_id =
                provider_name + ":" + user_info->provider_id;

#ifdef ETIL_MONGODB_ENABLED
            {
                auto default_role = config_.auth_config->role_for(user_id);
                etil::aaa::on_login_success(
                    config_.user_store, config_.audit_log,
                    user_info->email, user_id, default_role,
                    provider_name, "device_flow");
            }
#endif

            auto token = config_.jwt_auth->mint_token(
                user_id, user_info->email);
            auto role = config_.auth_config->role_for(user_id);

            nlohmann::json response = {
                {"token", token},
                {"role", role},
                {"expires_in", config_.auth_config->jwt_ttl_seconds}
            };
            res.set_content(response.dump(), "application/json");
        });

        // POST /auth/token — exchange a provider access token for ETIL JWT.
        //
        // Request:  { "provider": "github", "access_token": "gho_..." }
        // Response: { "token":"eyJ...", "role":"researcher",
        //             "expires_in":86400 }
        server_->Post("/auth/token", [this](const httplib::Request& req,
                                             httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");

            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body);
            } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})",
                                "application/json");
                return;
            }

            if (!body.contains("provider") ||
                !body["provider"].is_string() ||
                !body.contains("access_token") ||
                !body["access_token"].is_string()) {
                res.status = 400;
                res.set_content(
                    R"({"error":"Missing required fields: provider, access_token"})",
                    "application/json");
                return;
            }

            std::string provider_name = body["provider"].get<std::string>();
            std::string access_token =
                body["access_token"].get<std::string>();

            if (!config_.oauth_providers) {
                res.status = 400;
                res.set_content(
                    R"({"error":"No OAuth providers configured"})",
                    "application/json");
                return;
            }

            auto it = config_.oauth_providers->find(provider_name);
            if (it == config_.oauth_providers->end()) {
                res.status = 400;
                res.set_content(
                    R"({"error":"Unknown provider: )" + provider_name + "\"}",
                    "application/json");
                return;
            }

            auto user_info = it->second->get_user_info(access_token);
            if (!user_info) {
                res.status = 401;
                res.set_content(
                    R"({"error":"Invalid provider access token"})",
                    "application/json");
                return;
            }

            std::string user_id =
                provider_name + ":" + user_info->provider_id;

#ifdef ETIL_MONGODB_ENABLED
            {
                auto default_role = config_.auth_config->role_for(user_id);
                etil::aaa::on_login_success(
                    config_.user_store, config_.audit_log,
                    user_info->email, user_id, default_role,
                    provider_name, "token_exchange");
            }
#endif

            auto token = config_.jwt_auth->mint_token(
                user_id, user_info->email);
            auto role = config_.auth_config->role_for(user_id);

            nlohmann::json response = {
                {"token", token},
                {"role", role},
                {"expires_in", config_.auth_config->jwt_ttl_seconds}
            };
            res.set_content(response.dump(), "application/json");
        });

        // OPTIONS /auth/* — CORS preflight for all auth endpoints.
        auto auth_cors = [](const httplib::Request& /*req*/,
                            httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods",
                           "POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers",
                           "Content-Type, Authorization");
            res.set_header("Access-Control-Max-Age", "86400");
            res.status = 204;
        };
        server_->Options("/auth/device", auth_cors);
        server_->Options("/auth/poll", auth_cors);
        server_->Options("/auth/token", auth_cors);
    }
#endif

    // OPTIONS /mcp — CORS preflight
    server_->Options("/mcp", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers",
                        "Content-Type, Authorization, Mcp-Session-Id, Accept");
        res.set_header("Access-Control-Max-Age", "86400");
        res.status = 204;
    });
}

// ---------------------------------------------------------------------------
// Validation helpers
// ---------------------------------------------------------------------------

bool HttpTransport::validate_origin(const std::string& origin) const {
    if (config_.allowed_origins.empty()) {
        return true;
    }
    for (const auto& allowed : config_.allowed_origins) {
        if (origin == allowed) {
            return true;
        }
    }
    return false;
}

bool HttpTransport::validate_api_key(const std::string& auth_header) const {
    if (config_.api_key.empty()) {
        return true;
    }
    const std::string prefix = "Bearer ";
    if (auth_header.size() <= prefix.size()) {
        return false;
    }
    if (auth_header.substr(0, prefix.size()) != prefix) {
        return false;
    }
    return auth_header.substr(prefix.size()) == config_.api_key;
}

std::optional<nlohmann::json> HttpTransport::dispatch(const nlohmann::json& msg) {
    try {
        return handler_(msg);
    } catch (const std::exception& e) {
        fprintf(stderr, "MCP handler exception: %s\n", e.what());
        return make_error(nullptr, JsonRpcError::InternalError,
                          std::string("Server error: ") + e.what());
    } catch (...) {
        fprintf(stderr, "MCP handler unknown exception\n");
        return make_error(nullptr, JsonRpcError::InternalError,
                          "Unknown internal server error");
    }
}

#ifdef ETIL_JWT_ENABLED
bool HttpTransport::try_jwt_auth(const std::string& auth_header,
                                  std::string& out_user_id,
                                  std::string& out_role,
                                  std::string& out_email) const {
    if (!config_.jwt_auth) return false;

    const std::string prefix = "Bearer ";
    if (auth_header.size() <= prefix.size()) return false;
    if (auth_header.substr(0, prefix.size()) != prefix) return false;

    auto token = auth_header.substr(prefix.size());
    auto claims = config_.jwt_auth->validate_token(token);
    if (!claims) return false;

    out_user_id = claims->sub;
    out_role = claims->role;
    out_email = claims->email;
    return true;
}
#endif

} // namespace etil::mcp
