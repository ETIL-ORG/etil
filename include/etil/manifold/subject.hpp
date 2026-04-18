#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// ChannelSubject — hot/push queue that a route can append to and a
/// HeapObservable can drain. Owned jointly by the route's sink (which
/// keeps adding messages) and a HeapObservable of kind
/// ChannelSubscription (which drains them during execute_pipeline).
///
/// When the subject's last shared_ptr goes away, its destructor removes
/// the route from the channel service so the sink stops producing into
/// a dead queue. close() is called by the observable's destructor to
/// signal the execution loop to exit promptly.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

#include "etil/manifold/message.hpp"
#include "etil/manifold/route_spec.hpp"

namespace etil::manifold {

class ChannelService;

class ChannelSubject {
public:
    ChannelSubject() = default;
    ~ChannelSubject();

    ChannelSubject(const ChannelSubject&) = delete;
    ChannelSubject& operator=(const ChannelSubject&) = delete;

    /// Append a message. Notifies any pending wait_for().
    void push(Message msg);

    /// Wait up to `wait_ms` milliseconds for a message or close signal.
    /// Out-parameter `msg` receives the next queued message if one was
    /// available. Returns true if a message was extracted; false if the
    /// wait expired (msg untouched) or the subject closed with no
    /// remaining messages.
    bool pop_wait(Message& msg, int wait_ms);

    /// Signal the drain loop to exit. Non-draining — remaining queued
    /// messages are emitted first.
    void close();

    /// True if close() has been called. Queue may still be non-empty.
    bool is_closed() const;

    /// Number of queued messages not yet drained (approximate, for
    /// introspection).
    size_t queue_size() const;

    /// Set by ChannelService::observe() so the subject can remove its
    /// feeding route when it dies.
    void set_cleanup(std::weak_ptr<ChannelService> svc, RouteHandle handle);

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Message> queue_;
    bool closed_ = false;
    std::weak_ptr<ChannelService> cleanup_svc_;
    RouteHandle cleanup_handle_;
};

/// Build a sink that pushes each accepted message onto the shared
/// subject. Used by ChannelService::observe() to wire the route to
/// the subject.
std::shared_ptr<class ISink> make_subject_sink(std::shared_ptr<ChannelSubject> subject);

} // namespace etil::manifold
