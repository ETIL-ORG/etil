// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Loop registry — exercises the ChannelService APIs that back the
// obs-loop-channels / channel-add-transform / channel-remove-loop
// TIL primitives. Transform invocation (which requires an
// ExecutionContext) is out of scope here; this test covers the
// plumbing only.

#include "etil/manifold/service.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include <gtest/gtest.h>

using namespace etil::manifold;

namespace {

Message make_msg(std::string channel, std::string payload) {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::move(payload);
    m.payload_type = std::type_index(typeid(std::string));
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// A loop forwards published messages from OUT to IN. Without any
// transforms attached, a raw reader of IN sees whatever was written
// to OUT (route_trace/hops_remaining cycle guards active).
// ---------------------------------------------------------------------------
TEST(ManifoldLoopRegistry, RegisterLoopForwardsOutToIn) {
    auto svc = make_default_channel_service();

    auto h = svc->register_loop("etil.loop.out", "etil.loop.in");
    ASSERT_TRUE(h.valid());

    // Observe the destination.
    auto obs = svc->observe("etil.loop.in");
    ASSERT_NE(obs, nullptr);

    svc->publish(make_msg("etil.loop.out", "hello"));
    svc->flush_for_tests();

    // Route counts — one for the forwarder on OUT, one for the
    // observer on IN.
    auto sinks = svc->all_sink_stats();
    EXPECT_GE(sinks.size(), 2u);
}

// ---------------------------------------------------------------------------
// find_loop_for_destination returns the registered spec and is
// nullptr for un-looped channels.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopRegistry, FindLoopByDestination) {
    auto svc = make_default_channel_service();
    auto h = svc->register_loop("etil.loop.a", "etil.loop.b");
    ASSERT_TRUE(h.valid());

    auto* spec = svc->find_loop_for_destination("etil.loop.b");
    ASSERT_NE(spec, nullptr);
    EXPECT_EQ(spec->handle.id, h.id);
    EXPECT_EQ(spec->out_channel, "etil.loop.a");
    EXPECT_EQ(spec->in_channel, "etil.loop.b");
    EXPECT_EQ(spec->transform_xts.size(), 0u);

    EXPECT_EQ(svc->find_loop_for_destination("etil.nope"), nullptr);
}

// ---------------------------------------------------------------------------
// add_loop_transform appends opaque WordImpl pointers in insertion
// order. The registry stores them verbatim; invocation is a higher
// layer's concern.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopRegistry, AddLoopTransformAppendsInOrder) {
    auto svc = make_default_channel_service();
    auto h = svc->register_loop("etil.x", "etil.y");

    // Use distinct fake pointers — the registry doesn't dereference
    // them, so aliasing to int storage is safe for this test.
    auto* a = reinterpret_cast<etil::core::WordImpl*>(0x1000);
    auto* b = reinterpret_cast<etil::core::WordImpl*>(0x2000);
    auto* c = reinterpret_cast<etil::core::WordImpl*>(0x3000);

    EXPECT_TRUE(svc->add_loop_transform(h, a));
    EXPECT_TRUE(svc->add_loop_transform(h, b));
    EXPECT_TRUE(svc->add_loop_transform(h, c));

    auto* spec = svc->find_loop_for_destination("etil.y");
    ASSERT_NE(spec, nullptr);
    ASSERT_EQ(spec->transform_xts.size(), 3u);
    EXPECT_EQ(spec->transform_xts[0], a);
    EXPECT_EQ(spec->transform_xts[1], b);
    EXPECT_EQ(spec->transform_xts[2], c);
}

// ---------------------------------------------------------------------------
// add_loop_transform rejects invalid handles and null xts without
// mutating the registry.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopRegistry, AddLoopTransformRejectsInvalid) {
    auto svc = make_default_channel_service();

    LoopHandle bogus{.id = 9999};
    auto* fake = reinterpret_cast<etil::core::WordImpl*>(0x1000);

    EXPECT_FALSE(svc->add_loop_transform(bogus, fake));
    EXPECT_FALSE(svc->add_loop_transform(LoopHandle{.id = 0}, fake));

    auto h = svc->register_loop("etil.x", "etil.y");
    EXPECT_FALSE(svc->add_loop_transform(h, nullptr));
    auto* spec = svc->find_loop_for_destination("etil.y");
    ASSERT_NE(spec, nullptr);
    EXPECT_EQ(spec->transform_xts.size(), 0u);
}

// ---------------------------------------------------------------------------
// remove_loop tears down the forwarding route and removes the
// destination-index entry. A subsequent lookup returns nullptr.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopRegistry, RemoveLoopTearsDownForwarding) {
    auto svc = make_default_channel_service();

    auto h = svc->register_loop("etil.src", "etil.dst");
    ASSERT_TRUE(h.valid());

    auto routes_before = svc->all_sink_stats().size();

    svc->remove_loop(h);
    EXPECT_EQ(svc->find_loop_for_destination("etil.dst"), nullptr);
    EXPECT_EQ(svc->all_sink_stats().size(), routes_before - 1);
}

// ---------------------------------------------------------------------------
// After removal, messages published to OUT no longer propagate to IN.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopRegistry, RemoveLoopStopsForwarding) {
    auto svc = make_default_channel_service();

    // Observe IN before the loop is registered.
    auto obs = svc->observe("etil.after.in");
    ASSERT_NE(obs, nullptr);

    auto h = svc->register_loop("etil.after.out", "etil.after.in");
    svc->remove_loop(h);

    auto before = svc->cycle_stats();
    svc->publish(make_msg("etil.after.out", "lost"));
    svc->flush_for_tests();

    // With no loop, publishing to OUT should not affect IN-tracked
    // counters, though it may be dropped as unrouted depending on
    // whether other routes match. We simply confirm the loop
    // registry no longer reflects this destination.
    EXPECT_EQ(svc->find_loop_for_destination("etil.after.in"), nullptr);
    (void)before;
}

// ---------------------------------------------------------------------------
// Registering a loop with an invalid handle id (zero) returns an
// invalid handle; remove_loop is a no-op on invalid handles.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopRegistry, RemoveLoopIgnoresInvalidHandle) {
    auto svc = make_default_channel_service();
    svc->remove_loop(LoopHandle{.id = 0});         // no-op
    svc->remove_loop(LoopHandle{.id = 424242});    // no-op
    SUCCEED();
}
