// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Loopback integrity test — proves the Manifold infrastructure
// (async dispatcher, route engine, TTL cycle detection) correctly
// handles a bidirectional feedback loop between two channels.
//
// Topology:
//     etil.loop.a ──▶ a_sink ──republish──▶ etil.loop.b
//          ▲                                     │
//          └─────────────republish──── b_sink ◀──┘
//
// Each sink republishes with hops_remaining - 1. Dispatch rejects
// publishes with hops_remaining == 0, emitting a TTL-exhausted
// audit. This bounds the ping-pong loop deterministically without
// relying on timing.

#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <typeindex>

#include <gtest/gtest.h>

using namespace etil::manifold;

namespace {

/// Sink that counts accepts and, as long as the incoming message has
/// hops remaining, republishes it onto a target channel with
/// hops_remaining decremented by one. When dispatch sees
/// hops_remaining == 0 on a fresh publish, it rejects and emits a
/// TTL-exhausted audit — so the loop is bounded by the seed's
/// initial hop count, not by wall-clock time.
class PingPongSink : public ISink {
public:
    PingPongSink(ChannelService* svc, std::string target)
        : svc_(svc), target_(std::move(target)) {}

    void accept(const Message& m) override {
        count_.fetch_add(1, std::memory_order_relaxed);
        if (m.hops_remaining == 0) return;
        Message echo = m;
        echo.channel = target_;
        echo.hops_remaining = static_cast<uint8_t>(m.hops_remaining - 1);
        svc_->publish(std::move(echo));
    }

    uint64_t count() const {
        return count_.load(std::memory_order_relaxed);
    }

private:
    ChannelService* svc_;
    std::string target_;
    std::atomic<uint64_t> count_{0};
};

Message make_seed(std::string channel, uint8_t hops) {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::string("seed");
    m.payload_type = std::type_index(typeid(std::string));
    m.hops_remaining = hops;
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
// Ping-pong between two channels, bounded by the seed's hops_remaining.
//
// With hops_remaining=6 on the seed, the sequence on the async
// dispatcher is:
//   publish(a, hops=6) -> a_sink count=1 -> republish(b, hops=5)
//   publish(b, hops=5) -> b_sink count=1 -> republish(a, hops=4)
//   publish(a, hops=4) -> a_sink count=2 -> republish(b, hops=3)
//   publish(b, hops=3) -> b_sink count=2 -> republish(a, hops=2)
//   publish(a, hops=2) -> a_sink count=3 -> republish(b, hops=1)
//   publish(b, hops=1) -> b_sink count=3 -> republish(a, hops=0)
//   publish(a, hops=0) -> dispatch rejects, ttl_exhausted++
//
// Expected: count_a == count_b == 3, ttl_exhausted >= 1.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopback, TwoChannelPingPongBoundedByTtl) {
    auto svc = make_default_channel_service();

    auto a_sink = std::make_shared<PingPongSink>(svc.get(), "etil.loop.b");
    auto b_sink = std::make_shared<PingPongSink>(svc.get(), "etil.loop.a");

    ASSERT_TRUE(svc->add_route(make_spec("etil.loop.a", a_sink)).valid());
    ASSERT_TRUE(svc->add_route(make_spec("etil.loop.b", b_sink)).valid());

    auto before = svc->cycle_stats();

    svc->publish(make_seed("etil.loop.a", 6));
    svc->flush_for_tests();

    EXPECT_EQ(a_sink->count(), 3u);
    EXPECT_EQ(b_sink->count(), 3u);

    auto after = svc->cycle_stats();
    EXPECT_GE(after.ttl_exhausted, before.ttl_exhausted + 1u)
        << "ping-pong loop should terminate via TTL exhaustion";
}

// ---------------------------------------------------------------------------
// Zero-hop seed — proves dispatch rejects immediately when the
// seed itself already has hops_remaining == 0. Neither sink should
// fire, and ttl_exhausted increments by one.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopback, ZeroHopSeedIsRejectedImmediately) {
    auto svc = make_default_channel_service();

    auto a_sink = std::make_shared<PingPongSink>(svc.get(), "etil.loop.b");
    auto b_sink = std::make_shared<PingPongSink>(svc.get(), "etil.loop.a");
    svc->add_route(make_spec("etil.loop.a", a_sink));
    svc->add_route(make_spec("etil.loop.b", b_sink));

    auto before = svc->cycle_stats();

    svc->publish(make_seed("etil.loop.a", 0));
    svc->flush_for_tests();

    EXPECT_EQ(a_sink->count(), 0u);
    EXPECT_EQ(b_sink->count(), 0u);

    auto after = svc->cycle_stats();
    EXPECT_EQ(after.ttl_exhausted, before.ttl_exhausted + 1u);
}

// ---------------------------------------------------------------------------
// Higher hop count — prove the bound scales linearly. With
// hops_remaining=20 the loop delivers 10 to each side before TTL.
//
// Also proves: under sustained reentrant publishing, the
// dispatcher does NOT deadlock (the async-dispatch guarantee from
// Phase 5a.3 — publishers never block waiting for sinks, so a
// sink-from-dispatcher-thread calling publish re-enters the
// dispatcher queue without holding the service mutex).
// ---------------------------------------------------------------------------
TEST(ManifoldLoopback, LongerPingPongDoesNotDeadlock) {
    auto svc = make_default_channel_service();

    auto a_sink = std::make_shared<PingPongSink>(svc.get(), "etil.loop.b");
    auto b_sink = std::make_shared<PingPongSink>(svc.get(), "etil.loop.a");
    svc->add_route(make_spec("etil.loop.a", a_sink));
    svc->add_route(make_spec("etil.loop.b", b_sink));

    svc->publish(make_seed("etil.loop.a", 20));
    svc->flush_for_tests();

    EXPECT_EQ(a_sink->count(), 10u);
    EXPECT_EQ(b_sink->count(), 10u);
}
