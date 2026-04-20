// Copyright (c) 2026 Mark Deazley and the EvolutionaryTIL authors.
// SPDX-License-Identifier: BSD-3-Clause
//
// Phase 5a.5 — producer-keyed channel registry tests.
//
// Covers doc B §24.2's expectation that `channel-producer-list` and
// friends reflect every channel that has received a publish, whether
// or not a route is installed. Also exercises the injected-clock path
// so last_published_ns is deterministic under ManualClock.
//
// Plan test #11 (ManualClockDrivesLastPublishedNs) lives here rather
// than in test_manifold_async_dispatch.cpp — it's a producer-registry
// property, not a dispatcher invariant.

#include "etil/manifold/clock.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <typeindex>

#include <gtest/gtest.h>

using namespace etil::manifold;

namespace {

Message make_msg(std::string channel, std::string payload = "body") {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::move(payload);
    m.payload_type = std::type_index(typeid(std::string));
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// Empty-registry behavior
// ---------------------------------------------------------------------------

TEST(ProducerRegistry, EmptyServiceHasEmptyRegistry) {
    auto svc = make_default_channel_service();
    EXPECT_TRUE(svc->producer_list().empty());
    EXPECT_TRUE(svc->producers_by_pattern("etil.**").empty());

    auto stats = svc->producer_stats("etil.never.published");
    EXPECT_TRUE(stats.channel.empty());
    EXPECT_EQ(stats.published_count, 0u);
    EXPECT_EQ(stats.last_published_ns, 0u);
    EXPECT_EQ(stats.route_count, 0u);
}

// ---------------------------------------------------------------------------
// Producer-list reflects publishes even without routes
// ---------------------------------------------------------------------------

TEST(ProducerRegistry, ListIncludesChannelsWithoutRoutes) {
    auto svc = make_default_channel_service();
    svc->publish(make_msg("etil.a.only"));
    svc->publish(make_msg("etil.b.only"));
    svc->publish(make_msg("etil.a.only"));
    svc->flush_for_tests();

    auto list = svc->producer_list();
    std::set<std::string> names(list.begin(), list.end());
    EXPECT_EQ(names.count("etil.a.only"), 1u);
    EXPECT_EQ(names.count("etil.b.only"), 1u);
    EXPECT_EQ(names.size(), 2u);
}

// ---------------------------------------------------------------------------
// Per-channel stats: counts and timestamp
// ---------------------------------------------------------------------------

TEST(ProducerRegistry, StatsTrackPublishCount) {
    auto svc = make_default_channel_service();
    for (int i = 0; i < 7; ++i) svc->publish(make_msg("etil.counter"));
    svc->flush_for_tests();

    auto s = svc->producer_stats("etil.counter");
    EXPECT_EQ(s.channel, "etil.counter");
    EXPECT_EQ(s.published_count, 7u);
    EXPECT_GT(s.last_published_ns, 0u);
}

TEST(ProducerRegistry, StatsRouteCountReflectsLiveRoutes) {
    auto svc = make_default_channel_service();
    svc->publish(make_msg("etil.tracked"));
    svc->flush_for_tests();

    auto no_routes = svc->producer_stats("etil.tracked");
    EXPECT_EQ(no_routes.route_count, 0u);

    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.tracked";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    auto with_route = svc->producer_stats("etil.tracked");
    EXPECT_EQ(with_route.route_count, 1u);

    // Wildcards also count.
    RouteSpec wild;
    wild.channel_pattern = "etil.**";
    wild.sink = make_null_sink();
    svc->add_route(std::move(wild));

    auto both = svc->producer_stats("etil.tracked");
    EXPECT_EQ(both.route_count, 2u);
}

// ---------------------------------------------------------------------------
// Pattern filter
// ---------------------------------------------------------------------------

TEST(ProducerRegistry, PatternFilterMatchesWildcards) {
    auto svc = make_default_channel_service();
    svc->publish(make_msg("etil.mcp.request.received"));
    svc->publish(make_msg("etil.mcp.session.opened"));
    svc->publish(make_msg("etil.evolution.generation.start"));
    svc->publish(make_msg("etil.repl.stdout"));
    svc->flush_for_tests();

    auto mcp = svc->producers_by_pattern("etil.mcp.**");
    std::set<std::string> mcp_set(mcp.begin(), mcp.end());
    EXPECT_EQ(mcp_set.count("etil.mcp.request.received"), 1u);
    EXPECT_EQ(mcp_set.count("etil.mcp.session.opened"), 1u);
    EXPECT_EQ(mcp_set.size(), 2u);

    auto all = svc->producers_by_pattern("etil.**");
    EXPECT_EQ(all.size(), 4u);
}

// ---------------------------------------------------------------------------
// ManualClock drives last_published_ns (Plan test #11)
// ---------------------------------------------------------------------------

TEST(ProducerRegistry, ManualClockDrivesLastPublishedNs) {
    auto clock = make_manual_clock(1'000'000);
    auto svc = make_default_channel_service_with_clock(clock);

    svc->publish(make_msg("etil.clocked"));
    svc->flush_for_tests();

    auto s1 = svc->producer_stats("etil.clocked");
    EXPECT_EQ(s1.last_published_ns, 1'000'000u);
    EXPECT_EQ(s1.published_count, 1u);

    clock->set_time_ns(5'000'000);
    svc->publish(make_msg("etil.clocked"));
    svc->flush_for_tests();

    auto s2 = svc->producer_stats("etil.clocked");
    EXPECT_EQ(s2.last_published_ns, 5'000'000u);
    EXPECT_EQ(s2.published_count, 2u);
}

TEST(ProducerRegistry, LastPublishedNsMonotonicUnderAdvance) {
    auto clock = make_manual_clock(0);
    auto svc = make_default_channel_service_with_clock(clock);

    uint64_t prev = 0;
    for (int i = 0; i < 50; ++i) {
        clock->advance_ns(100);
        svc->publish(make_msg("etil.mono"));
        svc->flush_for_tests();
        auto now_ns = svc->producer_stats("etil.mono").last_published_ns;
        EXPECT_GT(now_ns, prev);
        prev = now_ns;
    }
}

// ---------------------------------------------------------------------------
// Concurrent publishers all counted
// ---------------------------------------------------------------------------

TEST(ProducerRegistry, ConcurrentPublishersAllCounted) {
    auto svc = make_default_channel_service();
    constexpr int kThreads = 8;
    constexpr int kPer = 500;

    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            std::string ch = "etil.race." + std::to_string(t);
            for (int i = 0; i < kPer; ++i) svc->publish(make_msg(ch));
        });
    }
    for (auto& t : ts) t.join();
    svc->flush_for_tests();

    EXPECT_EQ(svc->producer_list().size(),
              static_cast<size_t>(kThreads));
    for (int t = 0; t < kThreads; ++t) {
        auto s = svc->producer_stats("etil.race." + std::to_string(t));
        EXPECT_EQ(s.published_count, static_cast<uint64_t>(kPer));
    }
}

// ---------------------------------------------------------------------------
// Publishes denied by RBAC are NOT counted (only accepted publishes
// reach record_publisher; matches operator expectation that
// producer-list reflects what the service observed, not what was
// attempted).
//
// Note: we can't easily exercise the RBAC-denied path without a
// configured permissions object. The existing test_manifold_rbac
// suite already covers the denial mechanics; here we only assert
// that a successful publish is counted, which the other tests
// already demonstrate.
// ---------------------------------------------------------------------------
