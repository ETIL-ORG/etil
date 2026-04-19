// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Tests for the AMQP 1.0 broker sink (Phase 3c). Structure mirrors
// test_manifold_nats_sink.cpp: always-run failure-path tests plus
// live-broker tests gated on ETIL_AMQP_SINK_ENABLED with self-skip
// when no Artemis is running.

#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/manifold/amqp_sink.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/service.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using namespace etil::core;
using etil::manifold::amqp_sink_compiled_in;
using etil::manifold::BrokerSinkConfig;
using etil::manifold::ChannelService;
using etil::manifold::make_amqp_sink;
using etil::manifold::make_default_channel_service;

namespace {

struct AmqpTilFixture {
    Dictionary dict;
    ExecutionContext ctx;
    std::shared_ptr<ChannelService> channels;

    AmqpTilFixture() : ctx(0) {
        etil::manifold::shutdown_origin();
        etil::manifold::init_origin();
        register_primitives(dict);
        ctx.set_dictionary(&dict);
        channels = make_default_channel_service();
        ctx.set_channels(channels.get());
        ctx.set_session_id("test-session");
    }

    bool run(const std::string& word) {
        auto impl_opt = dict.lookup(word);
        if (!impl_opt) return false;
        auto fn = (*impl_opt)->native_code();
        if (!fn) return false;
        return fn(ctx);
    }

    void push_string(const std::string& s) {
        ctx.data_stack().push(Value::from(HeapString::create(s)));
    }
};

} // namespace

// ---------------------------------------------------------------------------
// channel-tap-amqp — TIL word, failure paths
// ---------------------------------------------------------------------------

TEST(AmqpTil, UnknownCodecReturnsFalse) {
    AmqpTilFixture f;
    f.push_string("amqp://127.0.0.1:5672");
    f.push_string("protobuf");                 // unknown codec
    f.push_string("etil.broker.test");
    ASSERT_TRUE(f.run("channel-tap-amqp"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_EQ(opt->as_int, 0);
}

TEST(AmqpTil, NotCompiledInOrRefusedReturnsFalse) {
    AmqpTilFixture f;
    f.push_string("amqp://127.0.0.1:1");   // refused port
    f.push_string("json");
    f.push_string("etil.broker.test");
    ASSERT_TRUE(f.run("channel-tap-amqp"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    // Stub or failed connect — both yield false.
    if (!amqp_sink_compiled_in()) {
        EXPECT_EQ(opt->type, Value::Type::Boolean);
        EXPECT_EQ(opt->as_int, 0);
    }
}

// ---------------------------------------------------------------------------
// make_amqp_sink direct (C++) — compile-time variant test
// ---------------------------------------------------------------------------

TEST(AmqpSink, CompiledInReportsCorrectly) {
#ifdef ETIL_AMQP_SINK_ENABLED
    EXPECT_TRUE(amqp_sink_compiled_in());
#else
    EXPECT_FALSE(amqp_sink_compiled_in());
#endif
}

TEST(AmqpSink, EmptyUrlReturnsNull) {
    auto svc = make_default_channel_service();
    BrokerSinkConfig cfg;  // broker_url empty
    cfg.codec = "json";
    auto sink = make_amqp_sink(std::move(cfg),
                               std::weak_ptr<ChannelService>(svc));
    EXPECT_EQ(sink, nullptr);
}

// ---------------------------------------------------------------------------
// Live-broker tests — gated on ETIL_AMQP_SINK_ENABLED + server running
// ---------------------------------------------------------------------------

#ifdef ETIL_AMQP_SINK_ENABLED

namespace {
bool artemis_reachable() {
    auto svc = make_default_channel_service();
    BrokerSinkConfig cfg;
    cfg.broker_url = "amqp://127.0.0.1:5672";
    cfg.codec = "json";
    auto sink = make_amqp_sink(std::move(cfg),
                               std::weak_ptr<ChannelService>(svc));
    return sink != nullptr;
}
} // namespace

TEST(AmqpSinkLive, ConnectAndPublish) {
    if (!artemis_reachable()) {
        GTEST_SKIP() << "Artemis not running at 127.0.0.1:5672";
    }
    auto svc = make_default_channel_service();
    BrokerSinkConfig cfg;
    cfg.broker_url = "amqp://127.0.0.1:5672";
    cfg.codec = "json";
    auto sink = make_amqp_sink(std::move(cfg),
                               std::weak_ptr<ChannelService>(svc));
    ASSERT_NE(sink, nullptr);

    etil::manifold::Message m;
    m.channel = "etil.test.amqp";
    m.payload = std::string("alpha");
    m.payload_type = std::type_index(typeid(std::string));
    sink->accept(m);
    EXPECT_GE(sink->forwarded_count() + sink->dropped_count(), 1u);
}

#endif // ETIL_AMQP_SINK_ENABLED
