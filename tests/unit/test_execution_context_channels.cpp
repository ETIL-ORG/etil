// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Phase 4a tests — ExecutionContext::out_ teed to etil.repl.stdout
// when a ChannelService is attached.

#include "etil/core/execution_context.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"

#include <any>
#include <memory>
#include <sstream>
#include <string>
#include <typeindex>

#include <gtest/gtest.h>

using namespace etil::core;
using etil::manifold::make_default_channel_service;
using etil::manifold::make_test_capture_sink;
using etil::manifold::RouteSpec;

namespace {

std::string payload_str(const etil::manifold::Message& m) {
    try { return std::any_cast<std::string>(m.payload); }
    catch (...) { return {}; }
}

} // namespace

TEST(ExecutionContextChannels, WithoutChannelsWritesGoDirectly) {
    ExecutionContext ctx(0);
    std::ostringstream os;
    ctx.set_out(&os);
    ctx.out() << "no-channels\n";
    ctx.out().flush();
    EXPECT_EQ(os.str(), "no-channels\n");
}

TEST(ExecutionContextChannels, AttachingChannelsInstallsTee) {
    ExecutionContext ctx(0);
    std::ostringstream os;
    ctx.set_out(&os);

    auto svc = make_default_channel_service();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.repl.stdout";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    ctx.set_channels(svc.get());
    ctx.set_session_id("sess-42");

    ctx.out() << "hello-line\n";
    ctx.out().flush();

    // Downstream still got the full line.
    EXPECT_NE(os.str().find("hello-line"), std::string::npos);

    // Channel subscriber saw it too. Hold the vector in a local so
    // references into it stay alive.
    ASSERT_GE(cap->size(), 1u);
    auto msgs = cap->captured();
    const auto& msg = msgs.front();
    EXPECT_EQ(msg.channel, "etil.repl.stdout");
    EXPECT_EQ(payload_str(msg), "hello-line\n");
    EXPECT_EQ(msg.tags.at("session_id"), "sess-42");
    EXPECT_EQ(msg.tags.at("output_kind"), "cr");
}

TEST(ExecutionContextChannels, DetachingChannelsRestoresOriginal) {
    ExecutionContext ctx(0);
    std::ostringstream os;
    ctx.set_out(&os);
    auto svc = make_default_channel_service();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.repl.stdout";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    ctx.set_channels(svc.get());
    ctx.out() << "during-tee\n";
    ctx.out().flush();
    size_t during_count = cap->size();

    ctx.set_channels(nullptr);
    ctx.out() << "after-tee\n";
    ctx.out().flush();

    // After detach no new channel messages arrive.
    EXPECT_EQ(cap->size(), during_count);

    // Downstream got both writes.
    EXPECT_NE(os.str().find("during-tee"), std::string::npos);
    EXPECT_NE(os.str().find("after-tee"), std::string::npos);
}

TEST(ExecutionContextChannels, PartialLineFlushedOnTeeDestruction) {
    auto svc = make_default_channel_service();
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.repl.stdout";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    {
        ExecutionContext ctx(0);
        std::ostringstream os;
        ctx.set_out(&os);
        ctx.set_channels(svc.get());
        ctx.out() << "no-newline-here";
        ctx.out().flush();
        ctx.set_channels(nullptr);
        // clear_stdout_tee flushes on destruction of the buf.
    }
    ASSERT_GE(cap->size(), 1u);
    auto msgs = cap->captured();
    EXPECT_EQ(payload_str(msgs.front()), "no-newline-here");
    EXPECT_EQ(msgs.front().tags.at("output_kind"), "stdout");
}
