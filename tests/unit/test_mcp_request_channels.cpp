// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Phase 4b tests — MCP request handlers publish lifecycle events
// on etil.mcp.request.** and etil.mcp.session.**.
//
// We exercise publish_request_event and publish_session_event
// directly (they are the public seams Phase 4b added) and assert
// shape + tags. A full dispatch_request integration test belongs
// in tests/docker/ alongside the existing MCP HTTP E2E.

#include "etil/mcp/mcp_server.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using etil::manifold::make_default_channel_service;
using etil::manifold::make_test_capture_sink;
using etil::manifold::RouteSpec;
using etil::mcp::McpServer;

namespace {

struct ServerFixture {
    McpServer server;
    std::shared_ptr<etil::manifold::ChannelService> channels =
        server.channels()
            ? std::shared_ptr<etil::manifold::ChannelService>(
                  std::shared_ptr<McpServer>{}, server.channels())
            : nullptr;
};

} // namespace

TEST(McpRequestChannels, PublishRequestEventEmitsOnReceivedChannel) {
    McpServer server;
    auto* svc = server.channels();
    ASSERT_NE(svc, nullptr);

    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.request.received";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    server.publish_request_event("received", "sess-1", "tools/call", -1);
    ASSERT_EQ(cap->size(), 1u);
    auto msgs = cap->captured();
    EXPECT_EQ(msgs.front().channel, "etil.mcp.request.received");
    EXPECT_EQ(msgs.front().tags.at("session_id"), "sess-1");
    EXPECT_EQ(msgs.front().tags.at("method"), "tools/call");
}

TEST(McpRequestChannels, PublishRequestEventCompletedCarriesLatency) {
    McpServer server;
    auto* svc = server.channels();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.request.completed";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    server.publish_request_event("completed", "sess-7", "ping", 12345);
    ASSERT_EQ(cap->size(), 1u);
    auto msgs = cap->captured();
    EXPECT_EQ(msgs.front().tags.at("latency_us"), "12345");
    EXPECT_EQ(msgs.front().tags.at("method"), "ping");
}

TEST(McpRequestChannels, PublishRequestEventFailedCarriesError) {
    McpServer server;
    auto* svc = server.channels();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.request.failed";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    server.publish_request_event("failed", "sess-9", "tools/call", 500,
                                   "bad-args");
    ASSERT_EQ(cap->size(), 1u);
    auto msgs = cap->captured();
    EXPECT_EQ(msgs.front().tags.at("error"), "bad-args");
}

TEST(McpRequestChannels, PublishRequestEventUnknownStageIsNoOp) {
    McpServer server;
    auto* svc = server.channels();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.request.**";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    server.publish_request_event("bogus-stage", "sess-1", "m", 0);
    EXPECT_EQ(cap->size(), 0u);
}

TEST(McpSessionChannels, OpenedEventEmitted) {
    McpServer server;
    auto* svc = server.channels();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.session.opened";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    server.publish_session_event("opened", "sess-11", "alice");
    ASSERT_EQ(cap->size(), 1u);
    auto msgs = cap->captured();
    EXPECT_EQ(msgs.front().tags.at("session_id"), "sess-11");
    EXPECT_EQ(msgs.front().tags.at("user_id"), "alice");
}

TEST(McpSessionChannels, ClosedEventEmitted) {
    McpServer server;
    auto* svc = server.channels();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.mcp.session.closed";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    server.publish_session_event("closed", "sess-11");
    ASSERT_EQ(cap->size(), 1u);
}
