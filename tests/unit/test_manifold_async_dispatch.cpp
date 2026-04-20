// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Phase 5a.2 — async-correctness test suite. Describes every property
// the Phase 5a.3 dispatcher refactor must satisfy. Disabled by default
// via a DISABLED_ prefix; Phase 5a.3 flips the build flag
// ETIL_MANIFOLD_ASYNC_DISPATCH_ENABLED to ON at which point every
// test must pass.
//
// Scope (plan §4 Phase 5a.2):
//   1. PublishReturnsBeforeSlowSubscriberCompletes
//   2. ReentrantPublishFromSubscriberDoesNotDeadlock
//   3. FlushForTestsBlocksUntilDispatchedItemsDelivered
//   4. FlushForTestsIsIdempotentWhenIdle
//   5. ConcurrentPublishersInterleave
//   6. DispatcherSurvivesSinkException
//   7. ShutdownDrainsPendingDeliveries
//   8. ShutdownInterruptsLongBlockingSink
//   9. LayerThreeEchoStateVisibleAfterFlush
//  10. OriginTupleIdenticalAcrossPublisherAndDispatcher
//
// Plan tests 11 (ManualClockDrivesLastPublishedNs) and 12
// (SubscriberQueueDepthVisibleInCycleStats) defer to Phase 5a.5
// (producer-registry, where last_published_ns lands) and Phase 5a.6
// (extended cycle stats). They're co-located with the features they
// exercise, not split across two test files.

#include "etil/manifold/clock.hpp"
#include "etil/manifold/rbac.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"
#include "etil/mcp/role_permissions.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <typeindex>

#include <gtest/gtest.h>

using namespace etil::manifold;
using etil::mcp::RolePermissions;

// ---------------------------------------------------------------------------
// Gate macro
// ---------------------------------------------------------------------------

#ifdef ETIL_MANIFOLD_ASYNC_DISPATCH_ENABLED
#define ASYNC_DISPATCH_TEST(suite, name) TEST(suite, name)
#else
#define ASYNC_DISPATCH_TEST(suite, name) TEST(suite, DISABLED_##name)
#endif

namespace {

Message make_msg(std::string channel = "etil.async.test",
                 std::string payload = "body") {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::move(payload);
    m.payload_type = std::type_index(typeid(std::string));
    return m;
}

RouteSpec make_spec(std::string pattern, std::shared_ptr<ISink> sink) {
    RouteSpec s;
    s.channel_pattern = std::move(pattern);
    s.sink = std::move(sink);
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. PublishReturnsBeforeSlowSubscriberCompletes
//   Proves: publisher's thread is not held hostage to subscriber work.
// ---------------------------------------------------------------------------

ASYNC_DISPATCH_TEST(AsyncDispatch, PublishReturnsBeforeSlowSubscriberCompletes) {
    auto svc = make_default_channel_service();
    auto slow = make_delaying_sink(std::chrono::milliseconds(500));
    svc->add_route(make_spec("etil.async.slow", slow));

    auto t0 = std::chrono::steady_clock::now();
    svc->publish(make_msg("etil.async.slow"));
    auto elapsed = std::chrono::steady_clock::now() - t0;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                  .count();
    EXPECT_LT(ms, 50) << "publish() returned after " << ms
                      << "ms; slow sink must not block publisher";

    // Subscriber has not finished yet — it's still sleeping on the
    // dispatcher thread. Flush to drain.
    svc->flush_for_tests();
    EXPECT_EQ(slow->count(), 1u);
}

// ---------------------------------------------------------------------------
// 2. ReentrantPublishFromSubscriberDoesNotDeadlock
//   Proves: a subscriber callback can publish onto another channel
//   without deadlocking against the service's mutex.
// ---------------------------------------------------------------------------

namespace {

/// Sink that republishes onto a second channel on every accept().
class RepublishingSink : public ISink {
public:
    RepublishingSink(ChannelService* svc, std::string rechannel)
        : svc_(svc), rechannel_(std::move(rechannel)) {}

    void accept(const Message& /*msg*/) override {
        Message m;
        m.channel = rechannel_;
        m.payload = std::string("re");
        m.payload_type = std::type_index(typeid(std::string));
        svc_->publish(std::move(m));
    }
    void flush() override {}

private:
    ChannelService* svc_;
    std::string rechannel_;
};

} // namespace

ASYNC_DISPATCH_TEST(AsyncDispatch,
                    ReentrantPublishFromSubscriberDoesNotDeadlock) {
    auto svc = make_default_channel_service();
    auto inner = make_test_capture_sink();
    svc->add_route(make_spec("etil.async.inner", inner));

    auto outer = std::make_shared<RepublishingSink>(svc.get(),
                                                    "etil.async.inner");
    svc->add_route(make_spec("etil.async.outer", outer));

    svc->publish(make_msg("etil.async.outer"));
    svc->flush_for_tests();

    EXPECT_EQ(inner->size(), 1u);
}

// ---------------------------------------------------------------------------
// 3. FlushForTestsBlocksUntilDispatchedItemsDelivered
//   Proves: flush is a correct fence. Every publish issued before
//   flush() has had its sink->accept() called by the time flush
//   returns.
// ---------------------------------------------------------------------------

ASYNC_DISPATCH_TEST(AsyncDispatch,
                    FlushForTestsBlocksUntilDispatchedItemsDelivered) {
    auto svc = make_default_channel_service();
    auto cnt = make_subscriber_counting_sink();
    svc->add_route(make_spec("etil.async.bulk", cnt));

    constexpr int kN = 1000;
    for (int i = 0; i < kN; ++i) {
        svc->publish(make_msg("etil.async.bulk"));
    }
    svc->flush_for_tests();
    EXPECT_EQ(cnt->count(), static_cast<uint64_t>(kN));
}

// ---------------------------------------------------------------------------
// 4. FlushForTestsIsIdempotentWhenIdle
//   Proves: flush() on an empty dispatcher returns promptly.
// ---------------------------------------------------------------------------

ASYNC_DISPATCH_TEST(AsyncDispatch, FlushForTestsIsIdempotentWhenIdle) {
    auto svc = make_default_channel_service();
    auto t0 = std::chrono::steady_clock::now();
    svc->flush_for_tests();
    svc->flush_for_tests();
    svc->flush_for_tests();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    EXPECT_LT(ms, 50) << "3 empty flushes took " << ms << "ms";
}

// ---------------------------------------------------------------------------
// 5. ConcurrentPublishersInterleave
//   Proves: concurrent publishers don't corrupt the queue; every
//   message is delivered exactly once.
// ---------------------------------------------------------------------------

ASYNC_DISPATCH_TEST(AsyncDispatch, ConcurrentPublishersInterleave) {
    auto svc = make_default_channel_service();
    auto cnt = make_subscriber_counting_sink();
    svc->add_route(make_spec("etil.async.parallel", cnt));

    constexpr int kThreads = 8;
    constexpr int kPer = 1000;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kPer; ++i) {
                svc->publish(make_msg("etil.async.parallel"));
            }
        });
    }
    for (auto& t : threads) t.join();

    svc->flush_for_tests();
    EXPECT_EQ(cnt->count(), static_cast<uint64_t>(kThreads * kPer));
}

// ---------------------------------------------------------------------------
// 6. DispatcherSurvivesSinkException
//   Proves: a throwing sink doesn't crash the dispatcher or the
//   publisher. Subsequent messages on the same route still deliver.
// ---------------------------------------------------------------------------

ASYNC_DISPATCH_TEST(AsyncDispatch, DispatcherSurvivesSinkException) {
    auto svc = make_default_channel_service();
    auto thrower = make_exception_injecting_sink(5, "boom");
    svc->add_route(make_spec("etil.async.throw", thrower));

    for (int i = 0; i < 10; ++i) {
        svc->publish(make_msg("etil.async.throw"));
    }
    svc->flush_for_tests();

    EXPECT_EQ(thrower->count(), 10u);
    EXPECT_EQ(thrower->thrown_count(), 1u);
}

// ---------------------------------------------------------------------------
// 7. ShutdownDrainsPendingDeliveries
//   Proves: tearing down the service delivers every already-queued
//   message before the destructor returns.
// ---------------------------------------------------------------------------

ASYNC_DISPATCH_TEST(AsyncDispatch, ShutdownDrainsPendingDeliveries) {
    auto cnt = make_subscriber_counting_sink();
    {
        auto svc = make_default_channel_service();
        svc->add_route(make_spec("etil.async.shutdown", cnt));
        constexpr int kN = 100;
        for (int i = 0; i < kN; ++i) {
            svc->publish(make_msg("etil.async.shutdown"));
        }
        // No flush — service dtor must drain on its own.
    }
    EXPECT_EQ(cnt->count(), 100u);
}

// ---------------------------------------------------------------------------
// 8. ShutdownInterruptsLongBlockingSink
//   Proves: a pathologically-blocked sink doesn't hang shutdown
//   forever. Strategy per plan §8.2: best-effort timed drain; leak
//   the worker thread if necessary and log a warning.
// ---------------------------------------------------------------------------

ASYNC_DISPATCH_TEST(AsyncDispatch, ShutdownInterruptsLongBlockingSink) {
    auto blocker = make_blocking_sink();
    auto t0 = std::chrono::steady_clock::now();
    {
        auto svc = make_default_channel_service();
        svc->add_route(make_spec("etil.async.block", blocker));
        svc->publish(make_msg("etil.async.block"));
        // Give the dispatcher a moment to park on the blocker.
        for (int i = 0; i < 100 && !blocker->accept_in_progress(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_TRUE(blocker->accept_in_progress());
        // Service goes out of scope here. Shutdown must return within
        // a bounded time window even though the blocker is stuck.
    }
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - t0)
                         .count();
    EXPECT_LT(elapsed_ms, 3000)
        << "Service dtor hung for " << elapsed_ms
        << "ms waiting on BlockingSink";
    // Clean up the leaked thread (if any) by releasing the sink so
    // its accept() returns; this prevents TSan from flagging a leak
    // from the previous shutdown's detach path.
    blocker->release();
}

// ---------------------------------------------------------------------------
// 9. LayerThreeEchoStateVisibleAfterFlush
//   Proves: echo suppression in deliver() (which now runs on the
//   dispatcher thread) still increments service-owned counters and
//   those counters are visible after flush_for_tests().
// ---------------------------------------------------------------------------

ASYNC_DISPATCH_TEST(AsyncDispatch, LayerThreeEchoStateVisibleAfterFlush) {
    auto svc = make_default_channel_service();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.async.echo";
    spec.sink = cap;
    spec.reject_own_origin = true;
    svc->add_route(std::move(spec));

    svc->publish(make_msg("etil.async.echo"));
    svc->flush_for_tests();

    EXPECT_EQ(cap->size(), 0u);
    EXPECT_EQ(svc->cycle_stats().echo_dropped, 1u);
}

// ---------------------------------------------------------------------------
// 10. OriginTupleIdenticalAcrossPublisherAndDispatcher
//   Proves: the MessageOrigin stamped by the publisher on the way
//   into dispatch is byte-identical to what the sink sees on the
//   dispatcher thread. No per-thread re-stamping.
// ---------------------------------------------------------------------------

ASYNC_DISPATCH_TEST(AsyncDispatch,
                    OriginTupleIdenticalAcrossPublisherAndDispatcher) {
    auto svc = make_default_channel_service();
    auto cap = make_test_capture_sink();
    svc->add_route(make_spec("etil.async.origin", cap));

    Message m = make_msg("etil.async.origin");
    m.tags["session_id"] = "sess-xyz";
    svc->publish(std::move(m));
    svc->flush_for_tests();

    ASSERT_EQ(cap->size(), 1u);
    const auto& got = cap->captured().front();
    EXPECT_EQ(got.origin.session_id, "sess-xyz");
    EXPECT_FALSE(got.origin.hostname.empty());
    EXPECT_GT(got.origin.app_startup_us, 0);
    EXPECT_GT(got.origin.seq, 0);
}
