// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Tests for broker sources (Phase 3d). Mostly failure-path coverage
// for the TIL words; live broker tests are gated on the sink flags
// and self-skip when no server is running.

#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/manifold/broker_source_factories.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/service.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using namespace etil::core;
using etil::manifold::BrokerSourceConfig;
using etil::manifold::ChannelService;
using etil::manifold::make_amqp_source;
using etil::manifold::make_default_channel_service;
using etil::manifold::make_nats_source;

namespace {

struct SourceTilFixture {
    Dictionary dict;
    ExecutionContext ctx;
    std::shared_ptr<ChannelService> channels;

    SourceTilFixture() : ctx(0) {
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
// channel-source-nats / -amqp — TIL failure paths
// ---------------------------------------------------------------------------

TEST(BrokerSourceTil, NatsUnknownCodecReturnsFalse) {
    SourceTilFixture f;
    f.push_string("nats://127.0.0.1:4222");
    f.push_string("avro");                // unknown codec
    f.push_string("etil.src.subject");
    f.push_string("etil.local.pattern");
    ASSERT_TRUE(f.run("channel-source-nats"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_EQ(opt->as_int, 0);
}

TEST(BrokerSourceTil, NatsRefusedPortReturnsFalse) {
    SourceTilFixture f;
    f.push_string("nats://127.0.0.1:1");   // refused
    f.push_string("json");
    f.push_string("etil.src.subject");
    f.push_string("etil.local.pattern");
    ASSERT_TRUE(f.run("channel-source-nats"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_EQ(opt->as_int, 0);
}

TEST(BrokerSourceTil, AmqpUnknownCodecReturnsFalse) {
    SourceTilFixture f;
    f.push_string("amqp://127.0.0.1:5672");
    f.push_string("avro");
    f.push_string("etil.src.addr");
    f.push_string("etil.local.pattern");
    ASSERT_TRUE(f.run("channel-source-amqp"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_EQ(opt->as_int, 0);
}

// ---------------------------------------------------------------------------
// mcp-notify-* — MCP Phase C helper words
// ---------------------------------------------------------------------------

TEST(McpNotifyTil, NatsRefusedUrlReturnsFalse) {
    SourceTilFixture f;
    f.push_string("nats://127.0.0.1:1");
    ASSERT_TRUE(f.run("mcp-notify-nats"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_EQ(opt->as_int, 0);
}

TEST(McpNotifyTil, AmqpRefusedUrlReturnsFalse) {
    SourceTilFixture f;
    f.push_string("amqp://127.0.0.1:1");
    ASSERT_TRUE(f.run("mcp-notify-amqp"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_EQ(opt->as_int, 0);
}

// ---------------------------------------------------------------------------
// Direct C++ factories — compiled-in probes + null returns on empty URL
// ---------------------------------------------------------------------------

TEST(BrokerSourceFactories, NatsCompiledInProbeMatchesDefine) {
#ifdef ETIL_NATS_SINK_ENABLED
    EXPECT_TRUE(etil::manifold::nats_source_compiled_in());
#else
    EXPECT_FALSE(etil::manifold::nats_source_compiled_in());
#endif
}

TEST(BrokerSourceFactories, AmqpCompiledInProbeMatchesDefine) {
#ifdef ETIL_AMQP_SINK_ENABLED
    EXPECT_TRUE(etil::manifold::amqp_source_compiled_in());
#else
    EXPECT_FALSE(etil::manifold::amqp_source_compiled_in());
#endif
}

TEST(BrokerSourceFactories, NatsEmptyUrlReturnsNull) {
    auto svc = make_default_channel_service();
    BrokerSourceConfig cfg;  // empty URL
    cfg.codec = "json";
    cfg.subject = "etil.x";
    cfg.channel_pattern = "etil.y";
    EXPECT_EQ(make_nats_source(std::move(cfg),
                               std::weak_ptr<ChannelService>(svc)),
              nullptr);
}

TEST(BrokerSourceFactories, AmqpEmptyUrlReturnsNull) {
    auto svc = make_default_channel_service();
    BrokerSourceConfig cfg;
    cfg.codec = "json";
    cfg.subject = "etil.x";
    cfg.channel_pattern = "etil.y";
    EXPECT_EQ(make_amqp_source(std::move(cfg),
                               std::weak_ptr<ChannelService>(svc)),
              nullptr);
}
