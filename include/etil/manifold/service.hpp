#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// ChannelService interface — the public entry point to Manifold.
///
/// Consumers obtain a std::shared_ptr<ChannelService> via constructor
/// injection (never a global singleton). Publishers call publish();
/// operators add and remove routes; observers subscribe via observe().
///
/// Implementations: DefaultChannelService (production) and
/// TestChannelService (deterministic in-memory capture for unit tests).

#include <memory>
#include <string>

#include "etil/manifold/message.hpp"
#include "etil/manifold/route_spec.hpp"

namespace etil::core {
class HeapObservable;
}

namespace etil::mcp {
struct RolePermissions;
}

namespace etil::manifold {

/// Per-route statistics snapshot — returned by channel-sink-stats.
struct SinkStats {
    RouteHandle handle;
    std::string channel_pattern;
    DeliveryMode delivery_mode = DeliveryMode::RingBuffered;
    size_t  buffer_capacity = 0;
    size_t  buffer_depth    = 0;   // current messages queued (RingBuffered)
    uint64_t accepted_count = 0;   // messages delivered to sink->accept
    uint64_t dropped_count  = 0;   // messages dropped at this route
};

/// Aggregate cycle-detection stats (channel-cycle-stats).
struct CycleStats {
    uint64_t cycles_detected    = 0;  // layer 1 (route_trace)
    uint64_t ttl_exhausted      = 0;  // layer 2 (hops_remaining)
    uint64_t echo_dropped       = 0;  // layer 3 (origin echo) — phase 3
    uint64_t static_warnings    = 0;  // add_route SCC warnings
};

/// Per-channel producer-side snapshot — returned by the Phase 5a.5
/// channel-producer-stats TIL word. Unlike SinkStats (which is
/// route-keyed), ProducerStats is keyed by channel *name* and
/// reflects publish activity whether or not any route is installed.
/// See doc B §24.2.
struct ProducerStats {
    std::string channel;              ///< channel name
    uint64_t published_count  = 0;    ///< total publish() calls on this channel
    uint64_t last_published_ns = 0;   ///< IClock::now_ns() at last publish
    uint64_t route_count      = 0;    ///< matching routes at query time
};

/// Result of a publish call. Never throws; reports diagnostic outcome.
struct PublishOutcome {
    bool accepted = true;     // at least one route accepted
    bool denied_by_rbac = false;
    bool cycle_blocked = false;
    bool ttl_exhausted = false;
    size_t routes_matched = 0;
    size_t routes_dropped = 0;   // overflow drops at matched routes
};

class ChannelService : public std::enable_shared_from_this<ChannelService> {
public:
    virtual ~ChannelService() = default;

    /// Publish a message onto its channel. Origin tuple is stamped here
    /// (`MessageOrigin::seq` is assigned from the process-global
    /// counter). Cycle checks and RBAC decisions happen inside.
    /// Never blocks; may drop (see doc B §22.2).
    ///
    /// When `principal` is nullptr, the call runs in standalone mode
    /// (all operations permitted) per RolePermissions convention.
    virtual PublishOutcome publish(Message msg,
                                   const etil::mcp::RolePermissions* principal = nullptr) = 0;

    /// Register a route. Returns a handle that can be passed to
    /// remove_route. Requires `channels_route_admin` when principal is
    /// non-null. Runs static SCC validation per doc B §20.6 and emits
    /// a warning on etil.logging.warn if a configured cycle is found.
    virtual RouteHandle add_route(RouteSpec spec,
                                  const etil::mcp::RolePermissions* principal = nullptr) = 0;

    /// Remove a previously-registered route. No-op if the handle is
    /// unknown. Requires `channels_route_admin` when principal is
    /// non-null.
    virtual void remove_route(RouteHandle handle,
                              const etil::mcp::RolePermissions* principal = nullptr) = 0;

    /// Obtain a HeapObservable that fires each time a message matching
    /// `channel_pattern` is published. Full wiring lands in Phase 2;
    /// Phase 1 returns nullptr (observable_sink available as a stub).
    virtual std::shared_ptr<etil::core::HeapObservable>
        observe(const std::string& channel_pattern,
                const etil::mcp::RolePermissions* principal = nullptr) = 0;

    /// Introspection — returns every active route's stats snapshot.
    virtual std::vector<SinkStats> all_sink_stats() const = 0;

    /// Introspection — aggregate cycle-detection counters.
    virtual CycleStats cycle_stats() const = 0;

    /// Compute the broker-side Session-Hmac header value for a given
    /// plaintext session_id, using a process-local CSPRNG key
    /// generated at construction. Returns empty string for empty
    /// session_id. The process key is never exposed (A-5 resolution).
    /// Output is a 22-character base64url string (128-bit HMAC-SHA256
    /// truncate).
    virtual std::string session_hmac(std::string_view session_id) const = 0;

    /// Test helper — block until every publish issued before this call
    /// has been fully delivered to its sinks. Default implementation
    /// is a no-op (sync implementations such as the current
    /// DefaultChannelService deliver on the caller's stack, so there
    /// is nothing to flush). The Phase 5a ThreadDispatcher override
    /// will actually block on the dispatcher queue.
    ///
    /// Introduced in Phase 5a.1 as a forward-compatibility seam: tests
    /// written today will be correct after Phase 5a.3 flips delivery
    /// onto a worker thread.
    virtual void flush_for_tests() {}

    // --- Phase 5a.5 producer registry (doc B §24.2) ---

    /// Every channel name that has received at least one publish()
    /// since service start. Not ordered (hash-map snapshot).
    virtual std::vector<std::string> producer_list() const { return {}; }

    /// Snapshot of a single channel's producer stats. Returns a
    /// ProducerStats with channel set to an empty string (and counts
    /// at their defaults) when the channel has never been published
    /// to.
    virtual ProducerStats producer_stats(std::string_view channel) const {
        (void)channel;
        return {};
    }

    /// producer_list() filtered to channels matching the pattern
    /// (same match semantics as route patterns: `*` single segment,
    /// `**` tail).
    virtual std::vector<std::string>
        producers_by_pattern(std::string_view pattern) const {
        (void)pattern;
        return {};
    }
};

/// Factory for the production implementation. Defined in
/// src/manifold/default_service.cpp.
std::shared_ptr<ChannelService> make_default_channel_service();

class IClock;

/// Test-oriented factory that lets callers inject a custom clock
/// (typically a ManualClock) for deterministic time-stamp assertions
/// in producer-registry tests. Production code should use the
/// default factory above.
std::shared_ptr<ChannelService> make_default_channel_service_with_clock(
    std::shared_ptr<IClock> clock);

} // namespace etil::manifold
