#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <cstdint>
#include <string>
#include <vector>

namespace etil::mcp {

/// Per-role permission set covering 6 resource domains.
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
};

} // namespace etil::mcp
