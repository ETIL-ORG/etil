// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// §17 Phase B — inbound SSE. Inbound client notifications (delivered
// via POST /mcp today, and via GET /mcp SSE stream once that endpoint
// is implemented) publish onto etil.mcp.in.** channels. TIL code
// subscribes via mcp-on-* convenience words.

#include "etil/manifold/rbac.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"
#include "etil/mcp/mcp_server.hpp"
#include "etil/mcp/role_permissions.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using etil::manifold::ChannelAction;
using etil::manifold::DeliveryMode;
using etil::manifold::RouteSpec;
using etil::manifold::is_hardwired;
using etil::manifold::hardwired_delivery_mode;
using etil::manifold::make_test_capture_sink;
using etil::mcp::McpServer;
using etil::mcp::RolePermissions;

// ---------------------------------------------------------------------------
// Hardwired etil.mcp.in.cancelled Read
// ---------------------------------------------------------------------------

TEST(SseIn, InCancelledReadIsHardwired) {
    EXPECT_TRUE(is_hardwired("etil.mcp.in.cancelled", ChannelAction::Read));
    // Write isn't hard-wired — the server publishes via the
    // DefaultChannelService's audit-free path; writes from TIL code
    // require explicit grant.
    EXPECT_FALSE(is_hardwired("etil.mcp.in.cancelled", ChannelAction::Write));
    EXPECT_EQ(hardwired_delivery_mode("etil.mcp.in.cancelled"),
              DeliveryMode::RingBuffered);
}

// ---------------------------------------------------------------------------
// publish_inbound_notification translates method → channel
// ---------------------------------------------------------------------------

TEST(SseIn, ProgressNotificationRoutesToProgressChannel) {
    McpServer server;
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.in.progress";
    spec.sink = capture;
    server.channels()->add_route(std::move(spec));

    nlohmann::json params = {{"progressToken", 1}, {"progress", 50}};
    server.publish_inbound_notification("notifications/progress", params);
    EXPECT_EQ(capture->size(), 1u);
    auto msgs = capture->captured();
    EXPECT_EQ(msgs[0].tags.at("method"), "notifications/progress");
}

TEST(SseIn, CancelledNotificationRoutesToCancelledChannel) {
    McpServer server;
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.in.cancelled";
    spec.sink = capture;
    server.channels()->add_route(std::move(spec));

    nlohmann::json params = {{"requestId", 42}};
    server.publish_inbound_notification("notifications/cancelled", params);
    EXPECT_EQ(capture->size(), 1u);
}

TEST(SseIn, RootsListChangedRoutesToRootsChannel) {
    McpServer server;
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.in.roots.changed";
    spec.sink = capture;
    server.channels()->add_route(std::move(spec));

    server.publish_inbound_notification("notifications/roots/list_changed",
                                        nlohmann::json::object());
    EXPECT_EQ(capture->size(), 1u);
}

TEST(SseIn, ArbitraryNotificationGoesToNotificationSubtree) {
    McpServer server;
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.in.notification.**";
    spec.sink = capture;
    server.channels()->add_route(std::move(spec));

    server.publish_inbound_notification("notifications/custom/event",
                                        nlohmann::json{{"k", "v"}});
    ASSERT_EQ(capture->size(), 1u);
    EXPECT_EQ(capture->captured()[0].channel,
              "etil.mcp.in.notification.custom.event");
}

TEST(SseIn, NonNotificationMethodIsIgnored) {
    McpServer server;
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.in.**";
    spec.sink = capture;
    server.channels()->add_route(std::move(spec));

    server.publish_inbound_notification("tools/call",
                                        nlohmann::json::object());
    EXPECT_EQ(capture->size(), 0u);
}

// ---------------------------------------------------------------------------
// RolePermissions inbound defaults
// ---------------------------------------------------------------------------

TEST(SseIn, RolePermissionsInboundDefaults) {
    RolePermissions r;
    EXPECT_FALSE(r.receive_client_notification);
    EXPECT_FALSE(r.receive_progress);
    EXPECT_TRUE(r.receive_cancelled);  // §17.5: cancellation default on
    EXPECT_FALSE(r.receive_roots_changed);
    EXPECT_EQ(r.mcp_subscribe_quota, 10);
}
