#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// ChannelAction bitmask and ChannelGrant struct used by the RBAC
/// extensions in include/etil/mcp/role_permissions.hpp. Declared in
/// its own header so a translation unit can depend on the enum without
/// pulling in the full Manifold service API.

#include <cstdint>
#include <string>

namespace etil::manifold {

/// Bitmask of per-channel actions a role may perform. Doc B §15.2.
enum class ChannelAction : uint8_t {
    None       = 0,
    Read       = 1 << 0,  // subscribe / observe
    Write      = 1 << 1,  // publish
    Route      = 1 << 2,  // install transforms or sinks on this channel
    Introspect = 1 << 3,  // list channels / list routes / query schema
};

inline constexpr uint8_t to_mask(ChannelAction a) {
    return static_cast<uint8_t>(a);
}

inline constexpr uint8_t operator|(ChannelAction a, ChannelAction b) {
    return static_cast<uint8_t>(to_mask(a) | to_mask(b));
}

inline constexpr uint8_t operator|(uint8_t mask, ChannelAction a) {
    return static_cast<uint8_t>(mask | to_mask(a));
}

inline constexpr uint8_t operator|(ChannelAction a, uint8_t mask) {
    return static_cast<uint8_t>(to_mask(a) | mask);
}

inline constexpr bool action_set(uint8_t mask, ChannelAction a) {
    return (mask & to_mask(a)) != 0;
}

/// Fine-grained per-pattern grant. Evaluated in the RBAC decision
/// procedure (doc B §15.3).
struct ChannelGrant {
    enum class Effect : uint8_t { Allow = 0, Deny = 1 };

    std::string pattern;            // e.g. "etil.evolution.**"
    uint8_t     actions = 0;        // bitmask of ChannelAction
    Effect      effect  = Effect::Allow;
};

} // namespace etil::manifold
