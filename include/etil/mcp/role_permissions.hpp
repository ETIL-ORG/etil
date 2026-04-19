#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <cstdint>
#include <string>
#include <vector>

#include "etil/manifold/channel_action.hpp"

namespace etil::mcp {

/// Per-role permission set covering 7 resource domains.
///
/// Plain data struct — no #ifdef guards, no JWT dependencies.
/// Standalone mode (permissions pointer is nullptr) means "all permitted".
struct RolePermissions {
    // --- System ---
    int max_sessions = 2;
    int instruction_budget = 10'000'000;
    bool allowlist_admin = false;
    bool list_sessions = false;
    bool session_kick = false;
    bool send_system_notification = false;
    bool send_user_notification = false;
    bool role_admin = false;
    int session_idle_timeout_seconds = 1800;  // 30 min default
    int interpret_execution_limit = 30;       // seconds per interpret call (0 = unlimited)
    int session_execution_limit = 0;          // cumulative seconds per session (0 = unlimited)

    // --- LVFS (Little Virtual File System) ---
    // lvfs_read is always true (design invariant, not stored)
    bool lvfs_modify = false;
    int64_t disk_quota = 1'048'576;  // 1 MB default

    // --- Network Client (outbound) ---
    bool net_client_allowed = false;
    std::vector<std::string> net_client_domains;  // {"*"} = all
    int net_client_quota = 100;

    // --- Network Server (inbound — schema only, enforced later) ---
    bool net_server_bind = false;
    bool net_server_tcp = false;
    bool net_server_udp = false;

    // --- Code Execution ---
    bool evaluate = true;
    bool evaluate_tainted = false;

    // --- Database (MongoDB) ---
    bool mongo_access = false;
    int  mongo_query_quota = 1000;  // queries per session

    // --- Channels (Manifold I/O pipeline) ---
    // See docs/claude-design/20260418B-IO-Channel-Pipeline-Architecture.md §15
    bool channels_enabled = false;              // primary on/off master switch
    std::vector<etil::manifold::ChannelGrant> channel_grants;
    int  channel_publish_quota = 1000;          // messages published per session
    int  channel_subscribe_quota = 10;          // concurrent subscriptions per session
    bool channels_route_admin = false;          // add/remove routes (dangerous)
    bool channels_network_sink = false;         // attach udp/tcp sinks (very dangerous)

    // --- MCP SSE inbound (§17.4) ---
    // Gate which inbound client notification types a session may subscribe
    // to via mcp-on-* TIL words. receive_cancelled defaults true so
    // cancellation is always honored unless explicitly disabled (and
    // even then, the hard-wired etil.mcp.in.cancelled Read bypass in
    // kHardwiredChannels ensures the session receives its own
    // cancellations).
    bool receive_client_notification = false;   // etil.mcp.in.notification.**
    bool receive_progress            = false;   // etil.mcp.in.progress
    bool receive_cancelled           = true;    // etil.mcp.in.cancelled
    bool receive_roots_changed       = false;   // etil.mcp.in.roots.changed
    int  mcp_subscribe_quota         = 10;      // concurrent mcp-on-* subscriptions
};

} // namespace etil::mcp
