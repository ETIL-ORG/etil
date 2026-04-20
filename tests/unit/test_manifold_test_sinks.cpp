// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Phase 5a.1 — smoke tests for the new test-sink helpers in
// include/etil/manifold/sinks.hpp. Each sink is exercised in
// isolation (no DefaultChannelService, no dispatcher) so later-phase
// tests can rely on the primitives behaving as advertised.

#include "etil/manifold/clock.hpp"
#include "etil/manifold/sinks.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

using etil::manifold::make_blocking_sink;
using etil::manifold::make_exception_injecting_sink;
using etil::manifold::make_manual_clock;
using etil::manifold::make_subscriber_counting_sink;
using etil::manifold::make_system_clock;
using etil::manifold::Message;

namespace {

Message make_msg(std::string channel = "etil.test",
                 std::string payload = "body") {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::move(payload);
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// SubscriberCountingSink
// ---------------------------------------------------------------------------

TEST(SubscriberCountingSinkSmoke, CountsEachAccept) {
    auto sink = make_subscriber_counting_sink();
    EXPECT_EQ(sink->count(), 0u);

    sink->accept(make_msg());
    sink->accept(make_msg());
    sink->accept(make_msg());
    EXPECT_EQ(sink->count(), 3u);

    sink->reset();
    EXPECT_EQ(sink->count(), 0u);
}

TEST(SubscriberCountingSinkSmoke, ConcurrentAcceptsAllCounted) {
    auto sink = make_subscriber_counting_sink();
    constexpr int kThreads = 8;
    constexpr int kPer = 500;
    std::vector<std::thread> ts;
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&] {
            for (int j = 0; j < kPer; ++j) sink->accept(make_msg());
        });
    }
    for (auto& t : ts) t.join();
    EXPECT_EQ(sink->count(), static_cast<uint64_t>(kThreads * kPer));
}

// ---------------------------------------------------------------------------
// BlockingSink
// ---------------------------------------------------------------------------

TEST(BlockingSinkSmoke, AcceptBlocksUntilReleased) {
    auto sink = make_blocking_sink();

    std::atomic<bool> finished{false};
    std::thread worker([&] {
        sink->accept(make_msg());
        finished.store(true, std::memory_order_release);
    });

    // Deterministic wait — no polling.
    sink->wait_until_accept_in_progress();
    EXPECT_TRUE(sink->accept_in_progress());
    EXPECT_FALSE(finished.load(std::memory_order_acquire));

    sink->release();
    worker.join();
    EXPECT_TRUE(finished.load(std::memory_order_acquire));
    EXPECT_EQ(sink->count(), 1u);
}

TEST(BlockingSinkSmoke, BlockReArmsAfterRelease) {
    auto sink = make_blocking_sink();
    sink->release();
    sink->accept(make_msg());  // returns immediately
    EXPECT_EQ(sink->count(), 1u);

    sink->block();
    std::atomic<bool> finished{false};
    std::thread worker([&] {
        sink->accept(make_msg());
        finished.store(true, std::memory_order_release);
    });
    sink->wait_until_accept_in_progress();
    EXPECT_FALSE(finished.load(std::memory_order_acquire));
    sink->release();
    worker.join();
    EXPECT_EQ(sink->count(), 2u);
}

// ---------------------------------------------------------------------------
// ExceptionInjectingSink
// ---------------------------------------------------------------------------

TEST(ExceptionInjectingSinkSmoke, ThrowsOnNthCallOnly) {
    auto sink = make_exception_injecting_sink(3, "boom");
    sink->accept(make_msg());
    sink->accept(make_msg());
    EXPECT_THROW(sink->accept(make_msg()), std::runtime_error);
    // 4th accept succeeds.
    sink->accept(make_msg());
    EXPECT_EQ(sink->count(), 4u);
    EXPECT_EQ(sink->thrown_count(), 1u);
}

TEST(ExceptionInjectingSinkSmoke, ZeroMeansNeverThrow) {
    auto sink = make_exception_injecting_sink(0);
    for (int i = 0; i < 10; ++i) sink->accept(make_msg());
    EXPECT_EQ(sink->count(), 10u);
    EXPECT_EQ(sink->thrown_count(), 0u);
}

// ---------------------------------------------------------------------------
// Clock helpers
// ---------------------------------------------------------------------------

TEST(ClockSmoke, SystemClockIsMonotonic) {
    auto c = make_system_clock();
    uint64_t a = c->now_ns();
    uint64_t b = c->now_ns();
    EXPECT_GE(b, a);
}

TEST(ClockSmoke, ManualClockReturnsSetValue) {
    auto c = make_manual_clock(42);
    EXPECT_EQ(c->now_ns(), 42u);
    c->set_time_ns(1000);
    EXPECT_EQ(c->now_ns(), 1000u);
    EXPECT_EQ(c->advance_ns(250), 1250u);
    EXPECT_EQ(c->now_ns(), 1250u);
}

// ---------------------------------------------------------------------------
// flush_for_tests base no-op is callable on every service
// ---------------------------------------------------------------------------

#include "etil/manifold/service.hpp"

TEST(ChannelServiceFlushForTestsSmoke, DefaultImplIsSilentNoOp) {
    auto svc = etil::manifold::make_default_channel_service();
    // Must be callable without crashing; no assertion on effect since
    // the sync impl's delivery is already complete before publish()
    // returns.
    svc->flush_for_tests();
    svc->flush_for_tests();
}

TEST(ChannelServiceFlushForTestsSmoke, ManualClockServiceFlushAlsoNoOp) {
    auto clock = make_manual_clock(1'000'000);
    auto svc = etil::manifold::make_default_channel_service_with_clock(clock);
    svc->flush_for_tests();
    EXPECT_EQ(clock->now_ns(), 1'000'000u);  // flush didn't advance clock
}
