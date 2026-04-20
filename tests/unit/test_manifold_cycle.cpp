// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"
#include "etil/manifold/transforms.hpp"

#include <gtest/gtest.h>

using namespace etil::manifold;

namespace {

Message make_msg(std::string channel, std::string payload = "") {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::move(payload);
    m.payload_type = std::type_index(typeid(std::string));
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// Layer 1 — route_trace cycle detection
// ---------------------------------------------------------------------------

TEST(Cycle, TraceHitRefusesRepublish) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.loop";
    spec.sink = capture;
    svc->add_route(std::move(spec));

    Message m = make_msg("etil.loop", "first");
    m.route_trace.push_back("etil.loop");  // already visited
    auto out = svc->publish(std::move(m));
    EXPECT_TRUE(out.cycle_blocked);
    EXPECT_FALSE(out.accepted);
    EXPECT_EQ(capture->size(), 0u);

    // Cycle audit record should have been emitted. We can't observe
    // it without a subscriber; just check the counter.
    auto stats = svc->cycle_stats();
    EXPECT_GE(stats.cycles_detected, 1u);
}

// ---------------------------------------------------------------------------
// Layer 2 — hops_remaining TTL
// ---------------------------------------------------------------------------

TEST(Cycle, TTLZeroRefusesDelivery) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.ttl";
    spec.sink = capture;
    svc->add_route(std::move(spec));

    Message m = make_msg("etil.ttl", "exhausted");
    m.hops_remaining = 0;
    auto out = svc->publish(std::move(m));
    EXPECT_TRUE(out.ttl_exhausted);
    EXPECT_FALSE(out.accepted);
    EXPECT_EQ(capture->size(), 0u);
    EXPECT_GE(svc->cycle_stats().ttl_exhausted, 1u);
}

// ---------------------------------------------------------------------------
// Hardwired audit channels bypass cycle layer 1 (so cycle audit can emit
// even when the audit sink is itself configured in a way that would
// re-trigger detection)
// ---------------------------------------------------------------------------

TEST(Cycle, AuditBypassAllowsCycleAuditToFlow) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.aaa.audit.**";
    spec.sink = capture;
    svc->add_route(std::move(spec));

    Message m = make_msg("etil.cycle-source", "first");
    m.route_trace.push_back("etil.cycle-source");
    svc->publish(std::move(m));  // triggers a cycle audit publish
    svc->flush_for_tests();

    // The audit emission should have reached the audit route sink.
    EXPECT_GE(capture->size(), 1u);
    auto msgs = capture->captured();
    bool found = false;
    for (const auto& x : msgs) {
        if (x.channel == "etil.aaa.audit.channel.cycle-detected") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}
