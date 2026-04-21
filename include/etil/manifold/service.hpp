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
#include <vector>

#include "etil/manifold/message.hpp"
#include "etil/manifold/route_spec.hpp"

namespace etil::core {
class HeapObservable;
class WordImpl;
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

/// Aggregate cycle-detection + dispatcher stats (channel-cycle-stats).
/// Phase 5a.6 added the lower four counters; they surface dispatcher-
/// internal state through the same TIL word operators already use for
/// cycle diagnostics.
struct CycleStats {
    uint64_t cycles_detected         = 0;  // layer 1 (route_trace)
    uint64_t ttl_exhausted           = 0;  // layer 2 (hops_remaining)
    uint64_t echo_dropped            = 0;  // layer 3 (origin echo)
    uint64_t static_warnings         = 0;  // add_route SCC warnings
    // --- Phase 5a.6 dispatcher counters ---
    uint64_t subscriber_queue_depth   = 0; // items currently queued
    uint64_t dropped_by_overflow      = 0; // RingBuffered/DropOldest drops (Phase 5a.7)
    uint64_t dispatcher_exceptions    = 0; // sink throws caught by dispatcher
    uint64_t dispatcher_idle_transitions = 0; // queue-empty edges
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

/// Opaque handle for a registered loop (obs-loop-channels). Loops
/// forward messages from one channel to another and own a mutable
/// transform chain that readers of the destination channel pick up
/// at `obs-message-read` time (model B1 per design discussion).
struct LoopHandle {
    uint64_t id = 0;
    bool valid() const { return id != 0; }
};

/// Snapshot of a single loop's state — returned by a future
/// introspection word. The transforms vector is a copy of the
/// opaque WordImpl pointers; invalid to dereference after any
/// dictionary mutation outside the caller's control.
struct LoopSpec {
    LoopHandle handle;
    std::string out_channel;          ///< source of the loop (where writes go in)
    std::string in_channel;           ///< destination (where reads come out)
    RouteHandle forward_route;        ///< the underlying OUT→IN forwarding route
    std::vector<etil::core::WordImpl*> transform_xts;  ///< lazy, reader-side chain
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

    // --- Loop registry (model B1 per design discussion) -----------------

    /// Register a loop from `out_channel` to `in_channel`. Installs a
    /// forwarding route so messages published to `out_channel` also
    /// appear on `in_channel` (with the route_trace/hops_remaining
    /// cycle guards applied on that re-publish). Returns a handle that
    /// later calls use to add transforms or tear down.
    ///
    /// Loops are keyed internally by `in_channel`; registering a second
    /// loop with the same destination replaces the prior one
    /// (its transform chain is dropped). The strawman model assumes
    /// one loop per destination; a future API could relax this.
    virtual LoopHandle register_loop(std::string out_channel,
                                     std::string in_channel,
                                     const etil::mcp::RolePermissions* principal = nullptr) {
        (void)out_channel; (void)in_channel; (void)principal;
        return {};
    }

    /// Append `xt` to the loop's transform chain. Transforms are
    /// consulted by `obs-message-read` each time a reader subscribes
    /// to the loop's destination channel; no effect on already-
    /// subscribed observables.
    virtual bool add_loop_transform(LoopHandle handle, etil::core::WordImpl* xt) {
        (void)handle; (void)xt;
        return false;
    }

    /// Look up the loop whose destination is `in_channel`. Returns
    /// nullptr when no loop is registered for that channel.
    virtual const LoopSpec* find_loop_for_destination(std::string_view in_channel) const {
        (void)in_channel;
        return nullptr;
    }

    /// Tear down a loop — removes the forwarding route and drops the
    /// transform chain. Any observable created by a prior
    /// `obs-message-read` call keeps its snapshot of the chain and is
    /// unaffected until it completes.
    virtual void remove_loop(LoopHandle handle,
                             const etil::mcp::RolePermissions* principal = nullptr) {
        (void)handle; (void)principal;
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
