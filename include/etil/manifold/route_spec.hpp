#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// RouteSpec — declarative wiring of a channel pattern to a sink through
/// zero or more transforms. Registered with ChannelService::add_route.
///
/// Delivery modes and overflow policy per doc B §22.2.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <absl/container/flat_hash_map.h>

#include "etil/manifold/sink.hpp"
#include "etil/manifold/transform.hpp"

namespace etil::manifold {

/// How a route's sink is driven.
enum class DeliveryMode : uint8_t {
    /// Fixed-size ring buffer; producer never blocks. Default for
    /// ordinary routes. Overflow policy selects which end drops.
    RingBuffered  = 0,

    /// No buffer; producer thread drives sink synchronously. Used for
    /// hard-wired audit/security/bootstrap channels where a drop is a
    /// correctness failure. Producer latency is bounded by sink write
    /// time (typically a file append).
    Inline        = 1,

    /// Inline with a short timeout — available for sinks that can
    /// conceivably wedge. Not in the default palette. Reserved for
    /// future use; behavior currently equivalent to Inline.
    InlineBounded = 2,
};

/// What to drop on RingBuffered overflow.
enum class OverflowPolicy : uint8_t {
    /// Drop the oldest buffered message (keep newest). Default —
    /// matches what MCP clients typically want to see (doc B §17.8).
    DropFirst = 0,

    /// Drop the newest arriving message (keep oldest). Useful for
    /// audit-style retention where first-seen is authoritative.
    DropLast  = 1,
};

inline constexpr size_t kDefaultRingCapacity = 16;

/// A route wires a channel pattern to a sink through a transform chain.
/// Pattern supports dotted channel names plus `*` (one segment) and
/// `**` (any depth). Tag filter requires exact key/value match on all
/// provided pairs (empty = no filter).
struct RouteSpec {
    std::string channel_pattern;
    absl::flat_hash_map<std::string, std::string> tag_filter;
    std::vector<std::shared_ptr<ITransform>> transforms;
    std::shared_ptr<ISink> sink;

    DeliveryMode   delivery_mode   = DeliveryMode::RingBuffered;
    size_t         buffer_capacity = kDefaultRingCapacity;
    OverflowPolicy overflow_policy = OverflowPolicy::DropFirst;
};

/// Opaque handle returned by add_route. Passed to remove_route and to
/// `channel-sink-stats` for introspection.
struct RouteHandle {
    uint64_t id = 0;

    bool valid() const { return id != 0; }
    bool operator==(const RouteHandle& o) const { return id == o.id; }
    bool operator!=(const RouteHandle& o) const { return id != o.id; }
};

} // namespace etil::manifold
