// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/session.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include "etil/fileio/uv_session.hpp"
#include "etil/lvfs/lvfs.hpp"

#ifdef ETIL_HTTP_CLIENT_ENABLED
#include "etil/net/http_client_config.hpp"
#include "etil/net/http_primitives.hpp"
#endif

#ifdef ETIL_JWT_ENABLED
#include "etil/mcp/auth_config.hpp"
#endif

#ifdef ETIL_MONGODB_ENABLED
#include "etil/db/mongo_client.hpp"
#include "etil/db/mongo_primitives.hpp"
#endif

#include <filesystem>

namespace etil::mcp {

Session::Session(const std::string& session_id,
                 const std::string& sessions_base_dir,
                 const std::string& library_dir)
    : id(session_id)
    , dict(std::make_unique<etil::core::Dictionary>()) {

    // Create per-session home directory if volume is configured
    if (!sessions_base_dir.empty()) {
        namespace fs = std::filesystem;
        home_dir = (fs::path(sessions_base_dir) / session_id).string();
        std::error_code ec;
        fs::create_directories(home_dir, ec);
        // Ignore errors — if creation fails, home_dir stays set but writes will fail
    }

    etil::core::register_primitives(*dict);
    interp = std::make_unique<etil::core::Interpreter>(*dict, interp_out, interp_err);

    // Configure path mapping before loading startup files
    if (!home_dir.empty()) {
        interp->set_home_dir(home_dir);
    }
    if (!library_dir.empty()) {
        interp->set_library_dir(library_dir);
    }

    // Create and wire LVFS
    if (!home_dir.empty()) {
        lvfs = std::make_unique<etil::lvfs::Lvfs>(home_dir, library_dir);
        interp->set_lvfs(lvfs.get());
    }

    // Create and wire UvSession for async file I/O
    uv_session = std::make_unique<etil::fileio::UvSession>();
    interp->context().set_uv_session(uv_session.get());

#ifdef ETIL_HTTP_CLIENT_ENABLED
    // Create and wire HTTP client state (config is shared across all sessions)
    {
        static const etil::net::HttpClientConfig http_config =
            etil::net::HttpClientConfig::from_env();
        if (http_config.enabled()) {
            http_state = std::make_unique<etil::net::HttpClientState>();
            http_state->config = &http_config;
            interp->context().set_http_client_state(http_state.get());
        }
    }
    etil::net::register_http_primitives(*dict);
#endif

#ifdef ETIL_MONGODB_ENABLED
    etil::db::register_mongo_primitives(*dict);
#endif

    // Register handler words as concepts so help.til can attach metadata
    interp->register_handler_words();

    // Load startup files (from baked-in data/, not from volumes)
    interp->load_startup_files({"data/builtins.til", "data/help.til"});

    // Discard any startup output
    out_buf.reset();
    interp_out.clear();
    err_buf.reset();
    interp_err.clear();

    stats.reset();
    last_activity = std::chrono::steady_clock::now();
}

#ifdef ETIL_JWT_ENABLED
void Session::apply_role_permissions(const RolePermissions& perms) {
#ifdef ETIL_HTTP_CLIENT_ENABLED
    // Override the server-wide HttpClientConfig with a role-specific one.
    if (http_state) {
        auto* role_config = new etil::net::HttpClientConfig();
        role_config->allowed_domains = perms.net_client_domains;
        // Inherit other settings from the server-wide config
        if (http_state->config) {
            role_config->max_response_bytes = http_state->config->max_response_bytes;
            role_config->request_timeout_ms = http_state->config->request_timeout_ms;
            role_config->per_interpret_budget = http_state->config->per_interpret_budget;
            role_config->per_session_budget = perms.net_client_quota;
            role_config->allow_http = http_state->config->allow_http;
            role_config->max_redirects = http_state->config->max_redirects;
        }
        role_http_config.reset(role_config);
        http_state->config = role_config;
    }
#endif

    // Store the pointer so tool_reset() can re-wire it after recreation.
    permissions_ptr = &perms;

    // Wire permissions pointer into the interpreter's execution context.
    // The pointer is stable — RolePermissions lives in AuthConfig::roles
    // for the server's lifetime.
    interp->context().set_permissions(&perms);
}
#endif // ETIL_JWT_ENABLED

Session::~Session() {
    if (interp) {
        interp->shutdown();
    }

    // Clean up per-session home directory
    if (!home_dir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(home_dir, ec);
        // Ignore errors on cleanup
    }
}

} // namespace etil::mcp
