#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Message envelope and origin tuple for Manifold channel pipeline.
///
/// Every message that flows through a ChannelService carries:
///   - a MessageOrigin — globally-unique identity tuple (§18)
///   - a channel — dotted routing key
///   - a timestamp — publish time (not part of identity)
///   - tags — string key/value routing/filtering metadata
///   - a payload — type-erased via std::any
///   - route_trace + hops_remaining — cycle-detection fields (§20)
///
/// See docs/claude-design/20260418B-IO-Channel-Pipeline-Architecture.md §3
/// for the design motivation and §18 for the identity-tuple contract.

#include <any>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>

#include <absl/container/flat_hash_map.h>
#include <absl/container/inlined_vector.h>

namespace etil::manifold {

/// Origin-type discriminator (A-4 resolution). Native fills hostname from
/// gethostname(); browser fills it from location.origin. Downstream
/// consumers use this enum instead of string-parsing hostname.
enum class OriginType : uint8_t {
    Native  = 0,
    Browser = 1,
};

/// Globally-unique identity tuple stamped on every published message.
///
/// The tuple (hostname, app_startup_us, session_id, seq) identifies exactly
/// one message across all processes, hosts, and time. `origin_type`
/// discriminates native vs browser producers without string-parsing
/// hostname.
///
/// hostname is a std::string_view pointing at process-global storage owned
/// by the origin module; it does not allocate per-message. session_id is
/// an owned string because it changes per session.
struct MessageOrigin {
    std::string_view hostname;
    int64_t          app_startup_us = 0;
    std::string      session_id;
    int64_t          seq = 0;
    OriginType       origin_type = OriginType::Native;
};

/// Cycle-detection defaults (§20).
inline constexpr uint8_t kDefaultHopsRemaining = 32;
inline constexpr size_t  kMaxRouteTraceEntries = 32;

/// Message envelope. Copyable and movable. std::any payload is type-erased
/// so channels are polymorphic over payload types; sinks that care about
/// the payload type check payload_type and cast.
struct Message {
    MessageOrigin origin;
    std::string   channel;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
    absl::flat_hash_map<std::string, std::string> tags;
    std::any payload;
    std::type_index payload_type = std::type_index(typeid(void));

    // Cycle detection (§20)
    absl::InlinedVector<std::string, 4> route_trace;
    uint8_t hops_remaining = kDefaultHopsRemaining;

    Message() = default;

    /// Convenience constructor for string payloads — the dominant case.
    static Message make_string(std::string channel, std::string text) {
        Message m;
        m.channel = std::move(channel);
        m.payload = std::move(text);
        m.payload_type = std::type_index(typeid(std::string));
        return m;
    }

    /// Fresh-identity factory (§20.5). Use when a transform deliberately
    /// constructs an aggregate/summary unrelated to its input messages;
    /// resets route_trace and hops_remaining.
    static Message fresh(std::string channel) {
        Message m;
        m.channel = std::move(channel);
        return m;
    }
};

} // namespace etil::manifold
