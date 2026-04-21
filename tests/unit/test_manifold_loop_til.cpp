// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// End-to-end TIL-level exercise of the loopback API:
// obs-create-channel, obs-message-write, obs-message-read,
// obs-loop-channels, channel-add-transform, channel-remove-loop,
// msg-payload. Goes through the dictionary lookup path (what TUI
// interpret calls reach) rather than directly invoking prim_* C++
// entry points.

#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_observable.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/observable_execution.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/service.hpp"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace etil::core;
using etil::manifold::ChannelService;
using etil::manifold::init_origin;
using etil::manifold::shutdown_origin;

namespace {

/// Fixture — registers all primitives, binds a fresh channel
/// service, opens an execution context.
struct LoopFixture {
    Dictionary dict;
    ExecutionContext ctx;
    std::shared_ptr<ChannelService> channels;

    LoopFixture() : ctx(0) {
        shutdown_origin();
        init_origin();
        register_primitives(dict);
        ctx.set_dictionary(&dict);
        channels = etil::manifold::make_default_channel_service();
        ctx.set_channels(channels.get());
    }

    bool run(const std::string& word) {
        auto impl_opt = dict.lookup(word);
        if (!impl_opt) return false;
        auto& impl = *impl_opt;
        auto fn = impl->native_code();
        if (!fn) return false;
        return fn(ctx);
    }

    void push_string(const std::string& s) {
        ctx.data_stack().push(Value::from(HeapString::create(s)));
    }

    /// Pop a Value::Type::Observable and keep it typed.
    HeapObservable* pop_observable() {
        auto opt = ctx.data_stack().pop();
        if (!opt || opt->type != Value::Type::Observable) return nullptr;
        return opt->as_observable();
    }
};

/// Drain a subscription observable into a vector of payload strings.
/// Runs synchronously via execute_pipeline (the same path obs-subscribe
/// takes in production). Caller is responsible for releasing the
/// observable when done.
std::vector<std::string> drain_payloads(HeapObservable* obs,
                                        ExecutionContext& ctx) {
    std::vector<std::string> out;
    Observer obsv = [&](Value v, ExecutionContext&) -> bool {
        if (v.type == Value::Type::Map) {
            Value p;
            if (v.as_map()->get("payload", p) && p.type == Value::Type::String) {
                out.emplace_back(p.as_string()->view());
                value_release(p);  // get() addref'd
            }
            v.as_map()->release();
        } else if (v.type == Value::Type::String) {
            out.emplace_back(v.as_string()->view());
            v.as_string()->release();
        }
        return true;
    };
    execute_pipeline(obs, ctx, obsv);
    return out;
}

/// Close all ChannelSubscriptions held under `obs` so drain_payloads
/// terminates. The Channel handle itself isn't a subscription, but
/// obs-message-read returned one with the loop's transforms chained.
void close_subscription_chain(HeapObservable* obs) {
    for (auto* cur = obs; cur; cur = cur->source()) {
        if (cur->obs_kind() == HeapObservable::Kind::ChannelSubscription) {
            cur->close_channel_subscription();
            break;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// obs-create-channel creates a Kind::Channel observable with the
// given name.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopTil, CreateChannelProducesChannelKind) {
    LoopFixture fx;
    fx.push_string("etil.test.alpha");
    ASSERT_TRUE(fx.run("obs-create-channel"));

    auto* obs = fx.pop_observable();
    ASSERT_NE(obs, nullptr);
    EXPECT_EQ(obs->obs_kind(), HeapObservable::Kind::Channel);
    EXPECT_NE(obs->channel_name(), nullptr);
    EXPECT_EQ(std::string(obs->channel_name()->view()), "etil.test.alpha");
    obs->release();
}

// ---------------------------------------------------------------------------
// obs-message-write on a channel handle publishes onto the underlying
// channel. Producer-stats tracks the publish activity.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopTil, MessageWritePublishesOnChannel) {
    LoopFixture fx;
    // chan-obs on stack
    fx.push_string("etil.test.beta");
    ASSERT_TRUE(fx.run("obs-create-channel"));

    // stack: [chan-obs, "hello"]
    fx.push_string("hello");
    ASSERT_TRUE(fx.run("obs-message-write"));

    auto ps = fx.channels->producer_stats("etil.test.beta");
    EXPECT_EQ(ps.channel, "etil.test.beta");
    EXPECT_EQ(ps.published_count, 1u);
}

// ---------------------------------------------------------------------------
// Loopback end-to-end — no transforms. A seed message on OUT
// appears on IN via the forwarding route.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopTil, LoopbackForwardsWithoutTransforms) {
    LoopFixture fx;

    // Register the loop: ( out-name in-name -- loop-handle )
    fx.push_string("etil.loop.out");
    fx.push_string("etil.loop.in");
    ASSERT_TRUE(fx.run("obs-loop-channels"));
    auto handle_opt = fx.ctx.data_stack().pop();
    ASSERT_TRUE(handle_opt);
    ASSERT_EQ(handle_opt->type, Value::Type::Integer);

    // Create an IN channel handle for reading.
    fx.push_string("etil.loop.in");
    ASSERT_TRUE(fx.run("obs-create-channel"));
    // obs-message-read pops chan-obs, pushes subscription obs.
    ASSERT_TRUE(fx.run("obs-message-read"));
    auto* sub_obs = fx.pop_observable();
    ASSERT_NE(sub_obs, nullptr);

    // Seed the loop by publishing onto OUT via another handle.
    fx.push_string("etil.loop.out");
    ASSERT_TRUE(fx.run("obs-create-channel"));
    fx.push_string("seed-raw");
    ASSERT_TRUE(fx.run("obs-message-write"));
    fx.channels->flush_for_tests();

    // Close the subscription so drain terminates; then drain.
    close_subscription_chain(sub_obs);
    auto drained = drain_payloads(sub_obs, fx.ctx);
    sub_obs->release();

    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained[0], "seed-raw");
}

// ---------------------------------------------------------------------------
// Loopback with one transform — the transform's xt runs on each
// read (lazy per design B1) and appends "-suffix" to the payload.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopTil, LoopbackAppliesOneTransform) {
    LoopFixture fx;

    // Define a transform in TIL: ( payload-map -- new-value true )
    // We'll extract the payload, append "-suffix", then push true
    // to indicate "keep". Registered in the dictionary as "xform-suffix".
    //
    // We can't easily write TIL colon definitions from C++ test, so
    // we register a C++ primitive that does the same thing and use
    // its xt.
    // Transform xt receives the payload string (obs-message-read auto-
    // unwraps the HeapMap). Signature: ( str -- str' bool ).
    auto xform = make_primitive(
        "xform-suffix",
        [](ExecutionContext& c) -> bool {
            auto opt = c.data_stack().pop();
            if (!opt || opt->type != Value::Type::String) return false;
            std::string s(opt->as_string()->view());
            opt->as_string()->release();
            s += "-suffix";
            c.data_stack().push(Value::from(HeapString::create(s)));
            c.data_stack().push(Value(true));
            return true;
        },
        {TypeSignature::Type::String},
        {TypeSignature::Type::String, TypeSignature::Type::Boolean});
    fx.dict.register_word("xform-suffix", std::move(xform));

    // Register the loop.
    fx.push_string("etil.xf.out");
    fx.push_string("etil.xf.in");
    ASSERT_TRUE(fx.run("obs-loop-channels"));
    auto handle_opt = fx.ctx.data_stack().pop();
    ASSERT_TRUE(handle_opt);
    uint64_t loop_id = static_cast<uint64_t>(handle_opt->as_int);

    // Attach the transform: ( loop-handle xt -- )
    fx.ctx.data_stack().push(Value(static_cast<int64_t>(loop_id)));
    auto xt_impl = fx.dict.lookup("xform-suffix");
    ASSERT_TRUE(xt_impl);
    Value xt_val;
    xt_val.type = Value::Type::Xt;
    xt_val.as_ptr = xt_impl->get();
    fx.ctx.data_stack().push(xt_val);
    ASSERT_TRUE(fx.run("channel-add-transform"));

    // Set up the reader.
    fx.push_string("etil.xf.in");
    ASSERT_TRUE(fx.run("obs-create-channel"));
    ASSERT_TRUE(fx.run("obs-message-read"));
    auto* sub_obs = fx.pop_observable();
    ASSERT_NE(sub_obs, nullptr);

    // Seed.
    fx.push_string("etil.xf.out");
    ASSERT_TRUE(fx.run("obs-create-channel"));
    fx.push_string("hello");
    ASSERT_TRUE(fx.run("obs-message-write"));
    fx.channels->flush_for_tests();

    // Close the inner subscription before draining.
    close_subscription_chain(sub_obs);
    auto drained = drain_payloads(sub_obs, fx.ctx);
    sub_obs->release();

    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained[0], "hello-suffix");
}

// ---------------------------------------------------------------------------
// Cancelable messaging — transform xt returns ( value' false ) drops
// the message. Next transform (if any) and the reader see nothing.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopTil, TransformReturnsFalseDropsMessage) {
    LoopFixture fx;

    // Filter-by-first-char — xt receives already-unwrapped string.
    auto xform = make_primitive(
        "xform-keep-k",
        [](ExecutionContext& c) -> bool {
            auto opt = c.data_stack().pop();
            if (!opt || opt->type != Value::Type::String) return false;
            std::string s(opt->as_string()->view());
            opt->as_string()->release();
            bool keep = !s.empty() && s[0] == 'k';
            c.data_stack().push(Value::from(HeapString::create(s)));
            c.data_stack().push(Value(keep));
            return true;
        },
        {TypeSignature::Type::String},
        {TypeSignature::Type::String, TypeSignature::Type::Boolean});
    fx.dict.register_word("xform-keep-k", std::move(xform));

    fx.push_string("etil.cancel.out");
    fx.push_string("etil.cancel.in");
    ASSERT_TRUE(fx.run("obs-loop-channels"));
    uint64_t loop_id = static_cast<uint64_t>(fx.ctx.data_stack().pop()->as_int);

    fx.ctx.data_stack().push(Value(static_cast<int64_t>(loop_id)));
    auto xt_impl = fx.dict.lookup("xform-keep-k");
    Value xt_val;
    xt_val.type = Value::Type::Xt;
    xt_val.as_ptr = xt_impl->get();
    fx.ctx.data_stack().push(xt_val);
    ASSERT_TRUE(fx.run("channel-add-transform"));

    fx.push_string("etil.cancel.in");
    ASSERT_TRUE(fx.run("obs-create-channel"));
    ASSERT_TRUE(fx.run("obs-message-read"));
    auto* sub_obs = fx.pop_observable();

    // Seed three messages; only "kept" passes the filter.
    for (const auto& m : {"dropped-1", "kept-me", "dropped-2"}) {
        fx.push_string("etil.cancel.out");
        ASSERT_TRUE(fx.run("obs-create-channel"));
        fx.push_string(m);
        ASSERT_TRUE(fx.run("obs-message-write"));
    }
    fx.channels->flush_for_tests();

    close_subscription_chain(sub_obs);
    auto drained = drain_payloads(sub_obs, fx.ctx);
    sub_obs->release();

    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained[0], "kept-me");
}

// ---------------------------------------------------------------------------
// channel-remove-loop stops forwarding — after removal, the
// destination channel receives nothing from OUT.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopTil, RemoveLoopStopsForwarding) {
    LoopFixture fx;

    fx.push_string("etil.rm.out");
    fx.push_string("etil.rm.in");
    ASSERT_TRUE(fx.run("obs-loop-channels"));
    auto handle_opt = fx.ctx.data_stack().pop();
    uint64_t loop_id = static_cast<uint64_t>(handle_opt->as_int);

    // Reader on IN before removal.
    fx.push_string("etil.rm.in");
    ASSERT_TRUE(fx.run("obs-create-channel"));
    ASSERT_TRUE(fx.run("obs-message-read"));
    auto* sub_before = fx.pop_observable();

    fx.push_string("etil.rm.out");
    ASSERT_TRUE(fx.run("obs-create-channel"));
    fx.push_string("first");
    ASSERT_TRUE(fx.run("obs-message-write"));
    fx.channels->flush_for_tests();

    // Now remove the loop and publish again — should not appear.
    fx.ctx.data_stack().push(Value(static_cast<int64_t>(loop_id)));
    ASSERT_TRUE(fx.run("channel-remove-loop"));

    fx.push_string("etil.rm.out");
    ASSERT_TRUE(fx.run("obs-create-channel"));
    fx.push_string("second");
    ASSERT_TRUE(fx.run("obs-message-write"));
    fx.channels->flush_for_tests();

    close_subscription_chain(sub_before);
    auto drained = drain_payloads(sub_before, fx.ctx);
    sub_before->release();

    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained[0], "first");
}
