// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// §17 Phase A outbound SSE rewire — verify:
//   1. McpServer exposes a ChannelService via channels().
//   2. Publishing onto etil.mcp.out.notification.system reaches the
//      registered bridge sink.
//   3. Publishing onto etil.mcp.out.notification.user with a
//      target_user_id tag reaches the targeted dispatch path.
//   4. An additional tap route can observe notifications going past
//      (route-as-observer pattern used by Phase 2 observe()).

#include "etil/mcp/mcp_server.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"

#include <gtest/gtest.h>

using etil::manifold::Message;
using etil::manifold::RouteSpec;
using etil::manifold::make_test_capture_sink;

namespace {

Message make_notification(std::string channel,
                          std::string session_id,
                          std::string text,
                          std::string target_user_id = "") {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::move(text);
    m.payload_type = std::type_index(typeid(std::string));
    m.tags["session_id"] = std::move(session_id);
    if (!target_user_id.empty()) {
        m.tags["target_user_id"] = std::move(target_user_id);
    }
    return m;
}

} // namespace

TEST(SseOut, ServerExposesChannelService) {
    etil::mcp::McpServer server;
    ASSERT_NE(server.channels(), nullptr);
}

TEST(SseOut, SystemNotificationPublishesAndIsTapped) {
    etil::mcp::McpServer server;
    auto capture = make_test_capture_sink();

    RouteSpec tap;
    tap.channel_pattern = "etil.mcp.out.notification.**";
    tap.sink = capture;
    auto handle = server.channels()->add_route(std::move(tap));
    ASSERT_TRUE(handle.valid());

    server.channels()->publish(make_notification(
        "etil.mcp.out.notification.system",
        "sess-abc",
        "hello world"));
    server.channels()->flush_for_tests();

    EXPECT_EQ(capture->size(), 1u);
    auto msgs = capture->captured();
    EXPECT_EQ(msgs[0].channel, "etil.mcp.out.notification.system");
    EXPECT_EQ(msgs[0].tags.at("session_id"), "sess-abc");
    EXPECT_EQ(std::any_cast<std::string>(msgs[0].payload), "hello world");
}

TEST(SseOut, UserNotificationCarriesTargetUserIdTag) {
    etil::mcp::McpServer server;
    auto capture = make_test_capture_sink();

    RouteSpec tap;
    tap.channel_pattern = "etil.mcp.out.notification.user";
    tap.sink = capture;
    server.channels()->add_route(std::move(tap));

    server.channels()->publish(make_notification(
        "etil.mcp.out.notification.user",
        "sess-abc",
        "targeted",
        "user-42"));
    server.channels()->flush_for_tests();

    ASSERT_EQ(capture->size(), 1u);
    auto msgs = capture->captured();
    EXPECT_EQ(msgs[0].tags.at("target_user_id"), "user-42");
}

TEST(SseOut, MessagesCarryMonotonicSequenceNumbers) {
    etil::mcp::McpServer server;
    auto capture = make_test_capture_sink();
    RouteSpec tap;
    tap.channel_pattern = "etil.mcp.out.notification.system";
    tap.sink = capture;
    server.channels()->add_route(std::move(tap));

    for (int i = 0; i < 5; ++i) {
        server.channels()->publish(make_notification(
            "etil.mcp.out.notification.system",
            "sess-x",
            "m" + std::to_string(i)));
    }
    server.channels()->flush_for_tests();

    auto msgs = capture->captured();
    ASSERT_EQ(msgs.size(), 5u);
    for (size_t i = 1; i < msgs.size(); ++i) {
        EXPECT_GT(msgs[i].origin.seq, msgs[i - 1].origin.seq);
    }
}

TEST(SseOut, HostnameStampedOnOutbound) {
    etil::mcp::McpServer server;
    auto capture = make_test_capture_sink();
    RouteSpec tap;
    tap.channel_pattern = "etil.mcp.out.notification.system";
    tap.sink = capture;
    server.channels()->add_route(std::move(tap));

    server.channels()->publish(make_notification(
        "etil.mcp.out.notification.system",
        "sess-h",
        "stamped"));
    server.channels()->flush_for_tests();

    auto msgs = capture->captured();
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_FALSE(msgs[0].origin.hostname.empty());
    EXPECT_GT(msgs[0].origin.app_startup_us, 0);
    EXPECT_EQ(msgs[0].origin.session_id, "sess-h");
}
