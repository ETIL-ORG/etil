#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Subscriber-dispatch interface — Phase 5a.3.
///
/// Separates "enqueue + worker-drive" from the DefaultChannelService so
/// (a) subscribers run on a thread isolated from the publisher,
/// closing the MCP response-path deadlock described in doc B §24.1; and
/// (b) tests can swap in an InlineDispatcher to verify delivery
/// semantics without involving threading.
///
/// Instances are owned by DefaultChannelService via std::unique_ptr;
/// the service's dtor calls shutdown() before any other member is
/// destroyed so in-flight closures see a live `this`.
///
/// Concurrency invariants (see implementation comments for detail):
///
///   I1. Every enqueue() call corresponds to exactly one deliver
///       invocation — the dispatcher does not merge or drop.
///   I2. flush() returns only after every enqueue() that preceded
///       it has had its delivery complete.
///   I3. shutdown() drains the queue before returning (except for
///       the pathological blocked-sink case, see §8.2).
///   I4. DeliveryItem holds Message by value; the worker thread
///       observes the exact origin tuple the publisher stamped.
///   I5. Sink exceptions are caught and counted; the worker
///       continues.
///   I6. Dispatch order within a single matched-route stream is
///       FIFO (single global queue preserves it).

#include <cstdint>
#include <functional>
#include <memory>

#include "etil/manifold/message.hpp"

namespace etil::manifold {

struct RouteState;  // defined in src/manifold/route_state.hpp — internal

/// One unit of delivery work: apply this route to this message.
/// Holds the RouteState by shared_ptr so the route outlives the
/// enqueue → deliver window even if the route is removed from the
/// service's map while the item is in flight.
struct DeliveryItem {
    std::shared_ptr<RouteState> state;
    Message msg;
};

/// Delivery callback supplied by the service at dispatcher
/// construction. The dispatcher invokes this once per popped
/// DeliveryItem. The callback is expected to run the layer-3 echo
/// check, apply transforms, and call the sink's accept(). See
/// DefaultChannelService::deliver() for the production flow.
using DeliveryFn = std::function<void(std::shared_ptr<RouteState>&,
                                       const Message&)>;

/// Snapshot of dispatcher-internal counters. Returned by stats().
struct DispatcherStats {
    uint64_t queue_depth           = 0;   ///< currently queued items
    uint64_t delivered_count       = 0;   ///< total deliveries attempted
    uint64_t dispatcher_exceptions = 0;   ///< sink throws caught
    uint64_t idle_transitions      = 0;   ///< queue-empty edges
};

class IDispatcher {
public:
    virtual ~IDispatcher() = default;

    /// Enqueue a delivery. Non-blocking for ThreadDispatcher;
    /// runs synchronously for InlineDispatcher.
    virtual void enqueue(DeliveryItem item) = 0;

    /// Block until every delivery enqueued before this call has
    /// completed. Returns promptly when the dispatcher is idle.
    /// Re-entrant enqueues (a delivery that itself enqueues) are
    /// included in the fence.
    virtual void flush() = 0;

    /// Idempotent. Drain the queue where possible and stop the
    /// worker. Bounded-time: if a sink is pathologically blocked,
    /// the worker is detached after a timeout (see §8.2).
    virtual void shutdown() = 0;

    /// Snapshot of the four internal counters.
    virtual DispatcherStats stats() const = 0;
};

/// Production dispatcher. One std::thread, MPSC queue under a
/// mutex + condition variable, shared_ptr-held internal state so
/// the worker survives safe dispatcher teardown.
std::unique_ptr<IDispatcher> make_thread_dispatcher(DeliveryFn deliver);

/// Test dispatcher that runs delivery synchronously inside
/// enqueue() on the caller's thread. Useful for tests that want
/// deterministic ordering without the complication of flush
/// fences. Ordering and FIFO guarantees still hold (there's only
/// one caller at a time per test).
std::unique_ptr<IDispatcher> make_inline_dispatcher(DeliveryFn deliver);

} // namespace etil::manifold
