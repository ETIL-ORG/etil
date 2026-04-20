// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// ChannelService::observe() + HeapObservable Kind::ChannelSubscription.
//
// Semantics: observe(pattern) returns a HeapObservable whose execution
// loop drains a shared ChannelSubject. Messages published on the
// matching channel are delivered to the subject by a route installed
// inside observe(); the route is removed automatically when the subject
// goes out of scope. Each emitted value is a HeapMap with keys
// channel / payload / session / seq / tags.
//
// Tests drive the subscription by:
//   1. Calling observe() to get the observable.
//   2. Publishing messages.
//   3. Closing the subject so the drain loop terminates.
//   4. Invoking execute_pipeline with a collecting observer.

#include "etil/core/execution_context.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_observable.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/observable_execution.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/subject.hpp"

#include <thread>
#include <typeindex>
#include <vector>

#include <gtest/gtest.h>

using namespace etil::core;
using etil::manifold::init_origin;
using etil::manifold::Message;
using etil::manifold::shutdown_origin;

namespace {

Message make_msg(std::string channel, std::string payload) {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::move(payload);
    m.payload_type = std::type_index(typeid(std::string));
    return m;
}

/// Run execute_pipeline in a background thread so the main thread can
/// close() the subject and unblock the drain loop. Returns the
/// collected HeapMap channel fields in publish order.
std::vector<std::string> drain_subject_in_thread(
    HeapObservable* obs, ExecutionContext& ctx,
    std::function<void()> work_between_start_and_close) {
    std::vector<std::string> channels;
    Observer observer = [&](Value v, ExecutionContext&) -> bool {
        if (v.type != Value::Type::Map) return true;
        auto* m = v.as_map();
        Value ch;
        if (m->get("channel", ch) && ch.type == Value::Type::String) {
            channels.emplace_back(ch.as_string()->view());
            ch.release();
        }
        m->release();
        return true;
    };
    std::thread worker([&] { execute_pipeline(obs, ctx, observer); });
    // Let the drain thread settle into pop_wait.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    work_between_start_and_close();
    worker.join();
    return channels;
}

} // namespace

// ---------------------------------------------------------------------------

TEST(ManifoldObservable, ObserveReturnsNonNull) {
    shutdown_origin();
    init_origin();
    auto svc = etil::manifold::make_default_channel_service();
    auto obs_sp = svc->observe("etil.test");
    ASSERT_NE(obs_sp, nullptr);
    EXPECT_EQ(obs_sp->obs_kind(), HeapObservable::Kind::ChannelSubscription);
}

TEST(ManifoldObservable, PublishesAppearOnSubscription) {
    shutdown_origin();
    init_origin();
    auto svc = etil::manifold::make_default_channel_service();

    // Grab a ref to the underlying subject so we can close() it from
    // the test thread. We do this by tapping the service's observe()
    // path but keep the subject alive via the returned HeapObservable
    // — we close via the observable's internal holder by releasing
    // after we're done.
    auto obs_sp = svc->observe("etil.obs.**");
    ASSERT_NE(obs_sp, nullptr);
    auto* obs = obs_sp.get();

    ExecutionContext ctx(0);
    auto collected = drain_subject_in_thread(obs, ctx, [&] {
        svc->publish(make_msg("etil.obs.one", "alpha"));
        svc->publish(make_msg("etil.obs.deep.two", "beta"));
        svc->publish(make_msg("etil.other", "ignored"));
        // Drain the dispatcher before closing the subject so the
        // ObservableSink has pushed all matched messages through.
        svc->flush_for_tests();
        // Close the subject by releasing the observable's holder —
        // done by releasing obs_sp's ref. Because obs_sp holds the
        // only external shared_ptr ref, this closes the subject.
        obs_sp.reset();
    });
    ASSERT_EQ(collected.size(), 2u);
    EXPECT_EQ(collected[0], "etil.obs.one");
    EXPECT_EQ(collected[1], "etil.obs.deep.two");
}

TEST(ManifoldObservable, ClosingSubjectTerminatesDrain) {
    shutdown_origin();
    init_origin();
    auto svc = etil::manifold::make_default_channel_service();
    auto obs_sp = svc->observe("etil.term");
    ASSERT_NE(obs_sp, nullptr);
    auto* obs = obs_sp.get();

    ExecutionContext ctx(0);
    auto collected = drain_subject_in_thread(obs, ctx, [&] {
        // No publishes; just close after a tick so the drain exits.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        obs_sp.reset();
    });
    EXPECT_TRUE(collected.empty());
}

TEST(ManifoldObservable, SessionAndSeqAreStamped) {
    shutdown_origin();
    init_origin();
    auto svc = etil::manifold::make_default_channel_service();
    auto obs_sp = svc->observe("etil.stamp");
    auto* obs = obs_sp.get();

    std::vector<int64_t> seqs;
    std::vector<std::string> sessions;
    ExecutionContext ctx(0);
    Observer observer = [&](Value v, ExecutionContext&) -> bool {
        auto* m = v.as_map();
        Value seq_v, sess_v;
        m->get("seq", seq_v);
        m->get("session", sess_v);
        seqs.push_back(seq_v.as_int);
        sessions.emplace_back(sess_v.as_string()->view());
        seq_v.release(); sess_v.release();
        m->release();
        return true;
    };

    std::thread worker([&] { execute_pipeline(obs, ctx, observer); });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    Message m1 = make_msg("etil.stamp", "a");
    m1.origin.session_id = "sess-x";
    svc->publish(std::move(m1));
    Message m2 = make_msg("etil.stamp", "b");
    m2.origin.session_id = "sess-y";
    svc->publish(std::move(m2));

    svc->flush_for_tests();  // drain dispatcher before closing subject
    obs_sp.reset();
    worker.join();

    ASSERT_EQ(seqs.size(), 2u);
    EXPECT_GT(seqs[1], seqs[0]);
    EXPECT_EQ(sessions[0], "sess-x");
    EXPECT_EQ(sessions[1], "sess-y");
}
