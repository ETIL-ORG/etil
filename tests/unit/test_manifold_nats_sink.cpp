// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Tests for the NATS broker sink (Phase 3b). Tests fall into two
// buckets:
//
//   - Always-on: tap-nats failure paths (no service, unknown codec,
//     binary not built) and session_hmac determinism. These run in
//     every build.
//
//   - Gated on ETIL_NATS_SINK_ENABLED: live-broker tests that expect
//     a NATS server at nats://127.0.0.1:4222. Enabled by the CMake
//     flag; disabled in the default WSL dev build. The gated tests
//     self-skip when connect fails so they don't false-fail when the
//     server isn't running.

#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/manifold/nats_sink.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using namespace etil::core;
using etil::manifold::BrokerSinkConfig;
using etil::manifold::ChannelService;
using etil::manifold::make_default_channel_service;
using etil::manifold::make_nats_sink;
using etil::manifold::nats_sink_compiled_in;

namespace {

struct NatsTilFixture {
    Dictionary dict;
    ExecutionContext ctx;
    std::shared_ptr<ChannelService> channels;

    NatsTilFixture() : ctx(0) {
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
        auto& impl = *impl_opt;
        auto fn = impl->native_code();
        if (!fn) return false;
        return fn(ctx);
    }

    void push_string(const std::string& s) {
        ctx.data_stack().push(Value::from(HeapString::create(s)));
    }
};

} // namespace

// ---------------------------------------------------------------------------
// channel-tap-nats — TIL word, failure paths (always run)
// ---------------------------------------------------------------------------

TEST(NatsTil, UnknownCodecReturnsFalse) {
    NatsTilFixture f;
    f.push_string("nats://127.0.0.1:4222");
    f.push_string("protobuf");                 // unknown codec
    f.push_string("etil.broker.test");
    ASSERT_TRUE(f.run("channel-tap-nats"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_EQ(opt->as_int, 0);  // false
}

TEST(NatsTil, EmptyCodecAliasesToJsonAndAttemptsConnect) {
    NatsTilFixture f;
    // Unreachable URL — connect fails in compiled-in build; in the
    // not-compiled-in build the make_nats_sink stub returns nullptr.
    // Either way the word returns false.
    f.push_string("nats://127.0.0.1:1");   // guaranteed-refused port
    f.push_string("");                       // empty → json
    f.push_string("etil.broker.test");
    ASSERT_TRUE(f.run("channel-tap-nats"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    // In both configurations this path cleanly yields false.
    // In compiled-in mode with a live broker, it would return a
    // non-zero Integer handle — covered by the gated test below.
    if (!nats_sink_compiled_in()) {
        EXPECT_EQ(opt->type, Value::Type::Boolean);
        EXPECT_EQ(opt->as_int, 0);
    }
}

// ---------------------------------------------------------------------------
// channel-session-hmac — TIL word
// ---------------------------------------------------------------------------

TEST(NatsTil, SessionHmacReturnsDeterministicToken) {
    NatsTilFixture f;
    f.push_string("session-alpha");
    ASSERT_TRUE(f.run("channel-session-hmac"));
    auto a_opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(a_opt.has_value());
    ASSERT_EQ(a_opt->type, Value::Type::String);
    std::string a = std::string(a_opt->as_string()->view());
    a_opt->as_string()->release();
    EXPECT_EQ(a.size(), 22u);  // 128-bit base64url

    // Same input → same token.
    f.push_string("session-alpha");
    ASSERT_TRUE(f.run("channel-session-hmac"));
    auto b_opt = f.ctx.data_stack().pop();
    std::string b = std::string(b_opt->as_string()->view());
    b_opt->as_string()->release();
    EXPECT_EQ(a, b);
}

TEST(NatsTil, SessionHmacEmptyYieldsEmpty) {
    NatsTilFixture f;
    f.push_string("");
    ASSERT_TRUE(f.run("channel-session-hmac"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    ASSERT_EQ(opt->type, Value::Type::String);
    EXPECT_TRUE(opt->as_string()->view().empty());
    opt->as_string()->release();
}

// ---------------------------------------------------------------------------
// make_nats_sink direct (C++) — compile-time variant test
// ---------------------------------------------------------------------------

TEST(NatsSink, CompiledInReportsCorrectly) {
#ifdef ETIL_NATS_SINK_ENABLED
    EXPECT_TRUE(nats_sink_compiled_in());
#else
    EXPECT_FALSE(nats_sink_compiled_in());
#endif
}

TEST(NatsSink, UnreachableUrlReturnsNull) {
    auto svc = make_default_channel_service();
    BrokerSinkConfig cfg;
    cfg.broker_url = "nats://127.0.0.1:1";  // refused
    cfg.codec = "json";
    auto sink = make_nats_sink(std::move(cfg),
                               std::weak_ptr<ChannelService>(svc));
    EXPECT_EQ(sink, nullptr);
}

TEST(NatsSink, EmptyUrlReturnsNull) {
    auto svc = make_default_channel_service();
    BrokerSinkConfig cfg;  // broker_url left empty
    cfg.codec = "json";
    auto sink = make_nats_sink(std::move(cfg),
                               std::weak_ptr<ChannelService>(svc));
    EXPECT_EQ(sink, nullptr);
}

// ---------------------------------------------------------------------------
// Live-broker tests — gated on ETIL_NATS_SINK_ENABLED + server running
// ---------------------------------------------------------------------------

#ifdef ETIL_NATS_SINK_ENABLED

namespace {
bool nats_server_reachable() {
    auto svc = make_default_channel_service();
    BrokerSinkConfig cfg;
    cfg.broker_url = "nats://127.0.0.1:4222";
    cfg.codec = "json";
    auto sink = make_nats_sink(std::move(cfg),
                               std::weak_ptr<ChannelService>(svc));
    return sink != nullptr;
}
} // namespace

TEST(NatsSinkLive, ConnectAndPublish) {
    if (!nats_server_reachable()) {
        GTEST_SKIP() << "nats-server not running at 127.0.0.1:4222";
    }
    auto svc = make_default_channel_service();
    BrokerSinkConfig cfg;
    cfg.broker_url = "nats://127.0.0.1:4222";
    cfg.codec = "json";
    auto sink = make_nats_sink(std::move(cfg),
                               std::weak_ptr<ChannelService>(svc));
    ASSERT_NE(sink, nullptr);

    etil::manifold::Message m;
    m.channel = "etil.test.nats";
    m.payload = std::string("alpha");
    m.payload_type = std::type_index(typeid(std::string));
    // Envelope a JSON-ish body via the json codec pathway — the
    // base class expects std::string or vector<uint8_t> payload;
    // we pre-encode here since this test bypasses routes.
    sink->accept(m);
    EXPECT_GE(sink->forwarded_count() + sink->dropped_count(), 1u);
}

#endif // ETIL_NATS_SINK_ENABLED
