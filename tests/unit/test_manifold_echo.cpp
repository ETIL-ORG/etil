// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Tests for cycle-detection layer 3 (origin echo suppression) and
// the associated RouteSpec::reject_own_origin flag. Phase 3d.

#include "etil/manifold/origin.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"

#include <memory>
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

TEST(EchoSuppression, DefaultRouteAcceptsOwnOrigin) {
    // Without reject_own_origin, a local publish with our own origin
    // must flow normally.
    auto svc = make_default_channel_service();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.echo.test";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    auto out = svc->publish(make_msg("etil.echo.test", "alpha"));
    EXPECT_TRUE(out.accepted);
    EXPECT_EQ(cap->size(), 1u);

    auto stats = svc->cycle_stats();
    EXPECT_EQ(stats.echo_dropped, 0u);
}

TEST(EchoSuppression, RejectOwnOriginDropsLocalPublish) {
    auto svc = make_default_channel_service();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.echo.test";
    spec.sink = cap;
    spec.reject_own_origin = true;
    svc->add_route(std::move(spec));

    auto out = svc->publish(make_msg("etil.echo.test", "beta"));
    // Publish itself returns accepted=true (RBAC passed, dispatch
    // ran); the route-level drop is reflected in cycle_stats.
    EXPECT_TRUE(out.accepted);
    EXPECT_EQ(cap->size(), 0u);

    auto stats = svc->cycle_stats();
    EXPECT_EQ(stats.echo_dropped, 1u);
}

TEST(EchoSuppression, EchoAuditIsEmitted) {
    auto svc = make_default_channel_service();
    auto dropper = make_test_capture_sink();
    auto audit = make_test_capture_sink();

    RouteSpec echo_route;
    echo_route.channel_pattern = "etil.echo.test";
    echo_route.sink = dropper;
    echo_route.reject_own_origin = true;
    svc->add_route(std::move(echo_route));

    RouteSpec audit_route;
    audit_route.channel_pattern = "etil.aaa.audit.channel.echo-dropped";
    audit_route.sink = audit;
    svc->add_route(std::move(audit_route));

    svc->publish(make_msg("etil.echo.test", "gamma"));
    EXPECT_EQ(dropper->size(), 0u);
    EXPECT_GE(audit->size(), 1u);

    auto msgs = audit->captured();
    EXPECT_EQ(msgs.front().tags.at("origin_channel"), "etil.echo.test");
}

TEST(EchoSuppression, HardwiredAuditChannelsNotRejected) {
    // Even with reject_own_origin on an audit-route, hardwired
    // audit channels flow — the check is bypassed for etil.aaa.audit.**
    auto svc = make_default_channel_service();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.aaa.audit.channel.echo-dropped";
    spec.sink = cap;
    spec.reject_own_origin = true;
    svc->add_route(std::move(spec));

    Message m = make_msg("etil.aaa.audit.channel.echo-dropped", "audit-body");
    svc->publish(std::move(m));
    EXPECT_EQ(cap->size(), 1u);
}
