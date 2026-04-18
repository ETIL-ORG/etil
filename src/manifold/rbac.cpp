// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/rbac.hpp"

#include <algorithm>

#include "etil/manifold/channel_name.hpp"
#include "etil/mcp/role_permissions.hpp"

namespace etil::manifold {

namespace {

/// The hard-wired channel table (doc B §15.5 + §22.2).
///
/// Each entry carries:
///   - pattern: the hard-wired channel pattern
///   - hardwired_actions: bitmask of actions that bypass role grants
///   - delivery_mode: Inline forces inline delivery for any route on
///     this pattern regardless of RouteSpec's declared mode
const std::vector<HardwiredEntry>& table() {
    static const std::vector<HardwiredEntry> kTable = {
        // Audit — Write hard-wired; Inline delivery. All AAA audit
        // records, including Manifold's own cycle/TTL/permission-denial
        // audits.
        {"etil.aaa.audit.**",
         ChannelAction::Write | ChannelAction::None,  // Write
         DeliveryMode::Inline},

        // Security — Write hard-wired; Inline delivery. Falco-style
        // alerts, AAA failure records.
        {"etil.security.**",
         ChannelAction::Write | ChannelAction::None,
         DeliveryMode::Inline},

        // Bootstrap — Write + Read hard-wired; Inline delivery. Startup
        // events must flow even when no role context exists.
        {"etil.system.bootstrap.**",
         ChannelAction::Write | ChannelAction::Read,
         DeliveryMode::Inline},

        // Logging-error safety valve — Write hard-wired; Inline
        // delivery. Any code may raise a log error even when its
        // channel access is otherwise revoked.
        {"etil.logging.error",
         ChannelAction::Write | ChannelAction::None,
         DeliveryMode::Inline},

        // Health — Write hard-wired; RingBuffered acceptable
        // (heartbeat can tolerate occasional drops).
        {"etil.health.**",
         ChannelAction::Write | ChannelAction::None,
         DeliveryMode::RingBuffered},

        // Manifold's own drop-visibility channels — Write hard-wired so
        // the service can always emit summaries/events; RingBuffered
        // delivery since these are themselves diagnostic.
        {"etil.manifold.sink.**",
         ChannelAction::Write | ChannelAction::None,
         DeliveryMode::RingBuffered},
    };
    return kTable;
}

int grant_specificity_rank(const ChannelGrant& g) {
    return pattern_specificity(g.pattern);
}

} // namespace

const std::vector<HardwiredEntry>& hardwired_channels() {
    return table();
}

bool is_hardwired(std::string_view channel, ChannelAction action) {
    for (const auto& e : table()) {
        if (channel_matches(e.pattern, channel) &&
            action_set(e.hardwired_actions, action)) {
            return true;
        }
    }
    return false;
}

DeliveryMode hardwired_delivery_mode(std::string_view channel) {
    for (const auto& e : table()) {
        if (channel_matches(e.pattern, channel)) {
            return e.delivery_mode;
        }
    }
    return DeliveryMode::RingBuffered;
}

Decision evaluate_access(const etil::mcp::RolePermissions* principal,
                         std::string_view channel,
                         ChannelAction action) {
    Decision d;

    // 1. Standalone mode — always allow.
    if (principal == nullptr) {
        d.allowed = true;
        d.reason = DecisionReason::Allowed_Standalone;
        return d;
    }

    // 2. Hard-wired bypass.
    if (is_hardwired(channel, action)) {
        d.allowed = true;
        d.reason = DecisionReason::Allowed_HardWired;
        return d;
    }

    // 3. Master switch off.
    if (!principal->channels_enabled) {
        d.allowed = false;
        d.reason = DecisionReason::Denied_MasterOff;
        return d;
    }

    // 4. Route action requires channels_route_admin.
    if (action == ChannelAction::Route && !principal->channels_route_admin) {
        d.allowed = false;
        d.reason = DecisionReason::Denied_RouteAdminRequired;
        return d;
    }

    // 5. Collect matching grants, sort by specificity (higher first),
    // with Deny ranking above Allow at equal specificity.
    struct ScoredGrant {
        const ChannelGrant* grant;
        int score;
    };
    std::vector<ScoredGrant> matches;
    matches.reserve(principal->channel_grants.size());
    for (const auto& g : principal->channel_grants) {
        if (channel_matches(g.pattern, channel)) {
            matches.push_back({&g, grant_specificity_rank(g)});
        }
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const ScoredGrant& a, const ScoredGrant& b) {
        if (a.score != b.score) return a.score > b.score;
        // Deny ranks above Allow at equal specificity.
        return static_cast<int>(a.grant->effect) >
               static_cast<int>(b.grant->effect);
    });

    // 6. Take the most-specific matching entry that carries the action.
    for (const auto& m : matches) {
        if (!action_set(m.grant->actions, action)) continue;
        if (m.grant->effect == ChannelGrant::Effect::Allow) {
            d.allowed = true;
            d.reason = DecisionReason::Allowed_ByGrant;
            d.matched_pattern = m.grant->pattern;
            return d;
        } else {
            d.allowed = false;
            d.reason = DecisionReason::Denied_ExplicitDeny;
            d.matched_pattern = m.grant->pattern;
            return d;
        }
    }

    // 7. Default deny.
    d.allowed = false;
    d.reason = DecisionReason::Denied_NoMatchingGrant;
    return d;
}

} // namespace etil::manifold
