// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Phase 2d role-admin TIL words + receive_* inbound-SSE gate.

#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/service.hpp"
#include "etil/mcp/role_permissions.hpp"

#include <memory>

#include <gtest/gtest.h>

using namespace etil::core;
using etil::mcp::RolePermissions;
using etil::manifold::ChannelGrant;

namespace {

struct RoleAdminFixture {
    Dictionary dict;
    ExecutionContext ctx;
    std::shared_ptr<etil::manifold::ChannelService> channels;
    RolePermissions perms;

    RoleAdminFixture() : ctx(0) {
        etil::manifold::shutdown_origin();
        etil::manifold::init_origin();
        register_primitives(dict);
        ctx.set_dictionary(&dict);
        channels = etil::manifold::make_default_channel_service();
        ctx.set_channels(channels.get());
        ctx.set_session_id("admin-sess");
        // Admin role with role_admin + master on. Tests that exercise
        // mcp-on-* add their own Read grant on etil.mcp.in.** since
        // the generic RBAC check is a separate layer from the
        // receive_* gate under test.
        perms.role_admin = true;
        perms.channels_enabled = true;
        ctx.set_permissions(&perms);
        ctx.set_mutable_permissions(&perms);
    }

    void grant_inbound_read() {
        ChannelGrant g;
        g.pattern = "etil.mcp.in.**";
        g.actions = etil::manifold::to_mask(etil::manifold::ChannelAction::Read);
        g.effect = ChannelGrant::Effect::Allow;
        perms.channel_grants.push_back(std::move(g));
    }

    bool run(const std::string& word) {
        auto impl_opt = dict.lookup(word);
        if (!impl_opt) return false;
        auto fn = (*impl_opt)->native_code();
        return fn && fn(ctx);
    }

    void push_str(const std::string& s) {
        ctx.data_stack().push(Value::from(HeapString::create(s)));
    }
    void push_bool(bool b) { ctx.data_stack().push(Value(b)); }
};

} // namespace

// ---------------------------------------------------------------------------
// role-grant-channel / role-revoke-channel
// ---------------------------------------------------------------------------

TEST(ManifoldRoleAdmin, GrantAddsGrantEntry) {
    RoleAdminFixture f;
    f.push_str("read,write");
    f.push_str("etil.**");
    f.push_str("me");
    ASSERT_TRUE(f.run("role-grant-channel"));
    ASSERT_EQ(f.perms.channel_grants.size(), 1u);
    EXPECT_EQ(f.perms.channel_grants[0].pattern, "etil.**");
    EXPECT_EQ(f.perms.channel_grants[0].effect, ChannelGrant::Effect::Allow);
}

TEST(ManifoldRoleAdmin, RevokeRemovesMatchingGrants) {
    RoleAdminFixture f;
    f.push_str("read"); f.push_str("etil.a"); f.push_str("me");
    ASSERT_TRUE(f.run("role-grant-channel"));
    f.push_str("write"); f.push_str("etil.b"); f.push_str("me");
    ASSERT_TRUE(f.run("role-grant-channel"));
    ASSERT_EQ(f.perms.channel_grants.size(), 2u);

    f.push_str("etil.a"); f.push_str("me");
    ASSERT_TRUE(f.run("role-revoke-channel"));
    ASSERT_EQ(f.perms.channel_grants.size(), 1u);
    EXPECT_EQ(f.perms.channel_grants[0].pattern, "etil.b");
}

TEST(ManifoldRoleAdmin, EnableFlipToggle) {
    RoleAdminFixture f;
    EXPECT_TRUE(f.perms.channels_enabled);
    f.push_bool(false);
    f.push_str("me");
    ASSERT_TRUE(f.run("role-channel-enable!"));
    EXPECT_FALSE(f.perms.channels_enabled);
    f.push_bool(true);
    f.push_str("me");
    ASSERT_TRUE(f.run("role-channel-enable!"));
    EXPECT_TRUE(f.perms.channels_enabled);
}

TEST(ManifoldRoleAdmin, NetworkSinkFlag) {
    RoleAdminFixture f;
    EXPECT_FALSE(f.perms.channels_network_sink);
    f.push_bool(true);
    f.push_str("me");
    ASSERT_TRUE(f.run("role-network-sink!"));
    EXPECT_TRUE(f.perms.channels_network_sink);
}

// ---------------------------------------------------------------------------
// Role admin requires role_admin permission + mutable slot
// ---------------------------------------------------------------------------

TEST(ManifoldRoleAdmin, GrantFailsWithoutRoleAdmin) {
    RoleAdminFixture f;
    f.perms.role_admin = false;
    f.push_str("read"); f.push_str("etil.**"); f.push_str("me");
    EXPECT_FALSE(f.run("role-grant-channel"));
}

TEST(ManifoldRoleAdmin, GrantFailsWithoutMutableSlot) {
    RoleAdminFixture f;
    f.ctx.set_mutable_permissions(nullptr);
    f.push_str("read"); f.push_str("etil.**"); f.push_str("me");
    EXPECT_FALSE(f.run("role-grant-channel"));
}

// ---------------------------------------------------------------------------
// receive_* gate on mcp-on-*
// ---------------------------------------------------------------------------

TEST(ManifoldRoleAdmin, McpOnProgressBlockedWhenReceiveOff) {
    RoleAdminFixture f;
    f.perms.receive_progress = false;
    EXPECT_FALSE(f.run("mcp-on-progress"));
}

TEST(ManifoldRoleAdmin, McpOnProgressAllowedWhenReceiveOn) {
    RoleAdminFixture f;
    f.grant_inbound_read();
    f.perms.receive_progress = true;
    ASSERT_TRUE(f.run("mcp-on-progress"));
    auto obs = f.ctx.data_stack().pop();
    ASSERT_TRUE(obs.has_value());
    EXPECT_EQ(obs->type, Value::Type::Observable);
    obs->release();
}

TEST(ManifoldRoleAdmin, McpOnCancelledAlwaysAllowed) {
    RoleAdminFixture f;
    // No grant + receive_cancelled off — both bypassed by the
    // hardwired Read rule on etil.mcp.in.cancelled.
    f.perms.receive_cancelled = false;
    ASSERT_TRUE(f.run("mcp-on-cancelled"));
    auto obs = f.ctx.data_stack().pop();
    ASSERT_TRUE(obs.has_value());
    obs->release();
}

TEST(ManifoldRoleAdmin, McpOnRootsChangedRespectsFlag) {
    RoleAdminFixture f;
    f.grant_inbound_read();
    f.perms.receive_roots_changed = false;
    EXPECT_FALSE(f.run("mcp-on-roots-changed"));
    f.perms.receive_roots_changed = true;
    ASSERT_TRUE(f.run("mcp-on-roots-changed"));
    auto obs = f.ctx.data_stack().pop();
    ASSERT_TRUE(obs.has_value());
    obs->release();
}

TEST(ManifoldRoleAdmin, McpOnNotificationRespectsFlag) {
    RoleAdminFixture f;
    f.perms.receive_client_notification = false;
    f.push_str("**");
    EXPECT_FALSE(f.run("mcp-on-notification"));
}
