#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// RBAC decision procedure and hard-wired channel table for Manifold.
///
/// Doc B §15 for the principles and §15.3 for the exact procedure.
/// Doc B §15.5 + §22.2 for the hard-wired table.

#include <string>
#include <string_view>
#include <vector>

#include "etil/manifold/channel_action.hpp"
#include "etil/manifold/route_spec.hpp"

namespace etil::mcp {
struct RolePermissions;
}

namespace etil::manifold {

/// Reason a decision was reached — for audit records.
enum class DecisionReason : uint8_t {
    Allowed_Standalone,
    Allowed_HardWired,
    Allowed_ByGrant,
    Denied_MasterOff,
    Denied_RouteAdminRequired,
    Denied_NetworkSinkRequired,
    Denied_NoMatchingGrant,
    Denied_ExplicitDeny,
    Denied_QuotaExhausted,
};

struct Decision {
    bool allowed = false;
    DecisionReason reason = DecisionReason::Denied_NoMatchingGrant;
    std::string matched_pattern;  // for Allowed_ByGrant / Denied_ExplicitDeny
};

/// Evaluate the decision procedure for `(principal, channel, action)`.
/// Does not consult or update per-session quotas — caller handles those.
///
/// principal == nullptr means standalone mode (always allow).
Decision evaluate_access(const etil::mcp::RolePermissions* principal,
                         std::string_view channel,
                         ChannelAction action);

/// One row in the hard-wired channel table (doc B §15.5 + §22.2).
struct HardwiredEntry {
    std::string_view pattern;
    uint8_t          hardwired_actions = 0;   // bitmask of ChannelAction
    DeliveryMode     delivery_mode     = DeliveryMode::RingBuffered;
};

/// The static hard-wired channel table. Decision procedure consults this
/// before evaluating role grants. Also drives delivery-mode default for
/// routes that attach to a hard-wired pattern.
const std::vector<HardwiredEntry>& hardwired_channels();

/// True if the given channel is hard-wired for the given action (any
/// pattern in the table matches and declares this action).
bool is_hardwired(std::string_view channel, ChannelAction action);

/// Returns the Inline delivery requirement for a channel, if any. If
/// the channel matches a hardwired entry whose delivery_mode is Inline,
/// returns Inline. Otherwise returns RingBuffered (the default — route
/// can still explicitly opt-in to Inline).
DeliveryMode hardwired_delivery_mode(std::string_view channel);

} // namespace etil::manifold
