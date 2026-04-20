// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Manifold TIL-word tests — exercise the Phase 2a primitives through
// the dictionary lookup path (closer to real TIL execution than direct
// C++ calls to `prim_*`). Each test registers all primitives, binds a
// fresh ChannelService to the context, invokes the word by name, and
// checks the stack + channel state.

#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"
#include "etil/mcp/role_permissions.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

#include <gtest/gtest.h>

using namespace etil::core;
using etil::manifold::ChannelGrant;
using etil::manifold::ChannelService;
using etil::manifold::RouteSpec;
using etil::manifold::TestCaptureSink;
using etil::manifold::init_origin;
using etil::manifold::shutdown_origin;

namespace {

struct ManifoldTilFixture {
    Dictionary dict;
    ExecutionContext ctx;
    std::shared_ptr<ChannelService> channels;

    ManifoldTilFixture() : ctx(0) {
        shutdown_origin();
        init_origin();
        register_primitives(dict);
        ctx.set_dictionary(&dict);
        channels = etil::manifold::make_default_channel_service();
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

    std::string pop_string() {
        auto opt = ctx.data_stack().pop();
        if (!opt || opt->type != Value::Type::String) return {};
        auto* hs = opt->as_string();
        std::string s(hs->view());
        hs->release();
        return s;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// channel-publish / channel-route-add / channel-list-routes
// ---------------------------------------------------------------------------

TEST(ManifoldTil, PublishReachesDirectlyAddedRoute) {
    ManifoldTilFixture f;
    auto capture = etil::manifold::make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.test.**";
    spec.sink = capture;
    f.channels->add_route(std::move(spec));

    f.push_string("payload-one");
    f.push_string("etil.test.deep.channel");
    ASSERT_TRUE(f.run("channel-publish"));
    f.channels->flush_for_tests();
    EXPECT_EQ(capture->size(), 1u);
    auto msgs = capture->captured();
    EXPECT_EQ(msgs[0].tags.at("session_id"), "test-session");
}

TEST(ManifoldTil, RouteAddReturnsHandleAndRoutesDeliver) {
    ManifoldTilFixture f;
    // ( detail kind pattern -- handle )
    f.push_string("");            // detail (ignored for null sink)
    f.push_string("null");        // kind
    f.push_string("etil.x");      // pattern
    ASSERT_TRUE(f.run("channel-route-add"));

    auto handle_opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(handle_opt.has_value());
    ASSERT_EQ(handle_opt->type, Value::Type::Integer);
    EXPECT_NE(handle_opt->as_int, 0);
}

TEST(ManifoldTil, TapFileCreatesFileRoute) {
    ManifoldTilFixture f;
    auto tmp = std::filesystem::temp_directory_path() /
               ("etil_manifold_til_" + std::to_string(::getpid()) + ".log");
    // ( path pattern -- handle )
    f.push_string(tmp.string());
    f.push_string("etil.tap.**");
    ASSERT_TRUE(f.run("channel-tap-file"));
    auto h = f.ctx.data_stack().pop();
    ASSERT_TRUE(h.has_value());
    EXPECT_NE(h->as_int, 0);

    f.push_string("from-til");
    f.push_string("etil.tap.out");
    ASSERT_TRUE(f.run("channel-publish"));
    f.channels->flush_for_tests();

    std::ifstream in(tmp);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("from-til"), std::string::npos);
    std::filesystem::remove(tmp);
}

TEST(ManifoldTil, ListRoutesReflectsRegistrations) {
    ManifoldTilFixture f;
    // Add two routes.
    f.push_string(""); f.push_string("null"); f.push_string("etil.a");
    ASSERT_TRUE(f.run("channel-route-add"));
    f.ctx.data_stack().pop();
    f.push_string(""); f.push_string("null"); f.push_string("etil.b");
    ASSERT_TRUE(f.run("channel-route-add"));
    f.ctx.data_stack().pop();

    ASSERT_TRUE(f.run("channel-list-routes"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    ASSERT_EQ(opt->type, Value::Type::Array);
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 2u);
    arr->release();
}

// ---------------------------------------------------------------------------
// channel-origin / channel-seq
// ---------------------------------------------------------------------------

TEST(ManifoldTil, OriginReturnsMapWithExpectedKeys) {
    ManifoldTilFixture f;
    ASSERT_TRUE(f.run("channel-origin"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    ASSERT_EQ(opt->type, Value::Type::Map);
    auto* m = opt->as_map();
    EXPECT_TRUE(m->has("host"));
    EXPECT_TRUE(m->has("startup"));
    EXPECT_TRUE(m->has("session"));
    EXPECT_TRUE(m->has("origintype"));

    Value session_v;
    ASSERT_TRUE(m->get("session", session_v));
    EXPECT_EQ(session_v.type, Value::Type::String);
    auto* hs = session_v.as_string();
    EXPECT_EQ(std::string(hs->view()), "test-session");
    hs->release();
    m->release();
}

TEST(ManifoldTil, SeqIsMonotonicAcrossCalls) {
    ManifoldTilFixture f;
    // Touch the counter by publishing once.
    auto capture = etil::manifold::make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.s";
    spec.sink = capture;
    f.channels->add_route(std::move(spec));

    ASSERT_TRUE(f.run("channel-seq"));
    auto s1 = f.ctx.data_stack().pop();
    ASSERT_TRUE(s1.has_value());

    f.push_string("x");
    f.push_string("etil.s");
    ASSERT_TRUE(f.run("channel-publish"));

    ASSERT_TRUE(f.run("channel-seq"));
    auto s2 = f.ctx.data_stack().pop();
    ASSERT_TRUE(s2.has_value());
    EXPECT_GT(s2->as_int, s1->as_int);
}

// ---------------------------------------------------------------------------
// channel-cycle-stats / channel-sink-stats / channel-all-sink-stats
// ---------------------------------------------------------------------------

TEST(ManifoldTil, CycleStatsReturnsExpectedKeys) {
    ManifoldTilFixture f;
    ASSERT_TRUE(f.run("channel-cycle-stats"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_EQ(opt->type, Value::Type::Map);
    auto* m = opt->as_map();
    // Cycle-detection keys (Phases 0-3).
    EXPECT_TRUE(m->has("cycles-detected"));
    EXPECT_TRUE(m->has("ttl-exhausted"));
    EXPECT_TRUE(m->has("echo-dropped"));
    EXPECT_TRUE(m->has("static-warnings"));
    // Dispatcher keys (Phase 5a.6).
    EXPECT_TRUE(m->has("subscriber-queue-depth"));
    EXPECT_TRUE(m->has("dropped-by-overflow"));
    EXPECT_TRUE(m->has("dispatcher-exceptions"));
    EXPECT_TRUE(m->has("dispatcher-idle-transitions"));
    m->release();
}

TEST(ManifoldTil, SinkStatsReturnsRouteFacts) {
    ManifoldTilFixture f;
    auto capture = etil::manifold::make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.stats";
    spec.sink = capture;
    auto handle = f.channels->add_route(std::move(spec));

    f.push_string("p"); f.push_string("etil.stats");
    ASSERT_TRUE(f.run("channel-publish"));
    f.channels->flush_for_tests();

    f.ctx.data_stack().push(Value(static_cast<int64_t>(handle.id)));
    ASSERT_TRUE(f.run("channel-sink-stats"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_EQ(opt->type, Value::Type::Map);
    auto* m = opt->as_map();
    Value accepted_v;
    ASSERT_TRUE(m->get("accepted-count", accepted_v));
    EXPECT_EQ(accepted_v.as_int, 1);
    m->release();
}

// ---------------------------------------------------------------------------
// channel-perm-check / channel-perm-list
// ---------------------------------------------------------------------------

TEST(ManifoldTil, PermCheckStandalonePermitsEverything) {
    ManifoldTilFixture f;
    // permissions() is nullptr by default — standalone.
    f.push_string("write");
    f.push_string("etil.any.channel");
    ASSERT_TRUE(f.run("channel-perm-check"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_EQ(opt->as_int, 1);
}

TEST(ManifoldTil, PermCheckRestrictedRoleDenies) {
    ManifoldTilFixture f;
    etil::mcp::RolePermissions r;
    r.channels_enabled = true;
    f.ctx.set_permissions(&r);

    f.push_string("write");
    f.push_string("etil.repl.stdout");
    ASSERT_TRUE(f.run("channel-perm-check"));
    auto opt = f.ctx.data_stack().pop();
    EXPECT_EQ(opt->as_int, 0);
}

TEST(ManifoldTil, PermListEmitsGrantMaps) {
    ManifoldTilFixture f;
    etil::mcp::RolePermissions r;
    r.channels_enabled = true;
    ChannelGrant g;
    g.pattern = "etil.foo.**";
    g.actions = etil::manifold::to_mask(etil::manifold::ChannelAction::Read);
    g.effect = ChannelGrant::Effect::Allow;
    r.channel_grants.push_back(std::move(g));
    f.ctx.set_permissions(&r);

    ASSERT_TRUE(f.run("channel-perm-list"));
    auto opt = f.ctx.data_stack().pop();
    ASSERT_EQ(opt->type, Value::Type::Array);
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 1u);
    arr->release();
}
