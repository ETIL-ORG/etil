// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/auth_config.hpp"
#include "etil/manifold/channel_action.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using etil::manifold::ChannelAction;
using etil::manifold::ChannelGrant;
using etil::mcp::AuthConfig;
using etil::mcp::RolePermissions;

TEST(AuthConfigChannels, ParseChannelsEnabledAndAdminFlags) {
    nlohmann::json j = {
        {"channels_enabled", true},
        {"channels_route_admin", true},
        {"channels_network_sink", true},
        {"channel_publish_quota", 500},
        {"channel_subscribe_quota", 5},
    };
    auto perms = AuthConfig::parse_role_permissions(j);
    EXPECT_TRUE(perms.channels_enabled);
    EXPECT_TRUE(perms.channels_route_admin);
    EXPECT_TRUE(perms.channels_network_sink);
    EXPECT_EQ(perms.channel_publish_quota, 500);
    EXPECT_EQ(perms.channel_subscribe_quota, 5);
}

TEST(AuthConfigChannels, ParseChannelGrantsWithAllActions) {
    nlohmann::json j = {
        {"channel_grants", nlohmann::json::array({
            {
                {"pattern", "etil.test.**"},
                {"actions", {"read", "write", "route", "introspect"}},
                {"effect", "allow"},
            },
            {
                {"pattern", "etil.secrets.**"},
                {"actions", {"read"}},
                {"effect", "deny"},
            },
        })},
    };
    auto perms = AuthConfig::parse_role_permissions(j);
    ASSERT_EQ(perms.channel_grants.size(), 2u);

    const auto& g0 = perms.channel_grants[0];
    EXPECT_EQ(g0.pattern, "etil.test.**");
    EXPECT_TRUE(g0.actions & static_cast<uint8_t>(ChannelAction::Read));
    EXPECT_TRUE(g0.actions & static_cast<uint8_t>(ChannelAction::Write));
    EXPECT_TRUE(g0.actions & static_cast<uint8_t>(ChannelAction::Route));
    EXPECT_TRUE(g0.actions & static_cast<uint8_t>(ChannelAction::Introspect));
    EXPECT_EQ(g0.effect, ChannelGrant::Effect::Allow);

    const auto& g1 = perms.channel_grants[1];
    EXPECT_EQ(g1.pattern, "etil.secrets.**");
    EXPECT_TRUE(g1.actions & static_cast<uint8_t>(ChannelAction::Read));
    EXPECT_EQ(g1.effect, ChannelGrant::Effect::Deny);
}

TEST(AuthConfigChannels, ChannelGrantEffectDefaultsToAllow) {
    nlohmann::json j = {
        {"channel_grants", nlohmann::json::array({
            {
                {"pattern", "etil.**"},
                {"actions", {"write"}},
            },
        })},
    };
    auto perms = AuthConfig::parse_role_permissions(j);
    ASSERT_EQ(perms.channel_grants.size(), 1u);
    EXPECT_EQ(perms.channel_grants[0].effect, ChannelGrant::Effect::Allow);
}

TEST(AuthConfigChannels, ChannelGrantSkipsInvalidEffect) {
    nlohmann::json j = {
        {"channel_grants", nlohmann::json::array({
            {
                {"pattern", "etil.**"},
                {"actions", {"read"}},
                {"effect", "yolo"},
            },
        })},
    };
    auto perms = AuthConfig::parse_role_permissions(j);
    EXPECT_TRUE(perms.channel_grants.empty());
}

TEST(AuthConfigChannels, ChannelGrantSkipsUnknownActions) {
    nlohmann::json j = {
        {"channel_grants", nlohmann::json::array({
            {
                {"pattern", "etil.**"},
                {"actions", {"read", "telepathy", "write"}},
            },
        })},
    };
    auto perms = AuthConfig::parse_role_permissions(j);
    ASSERT_EQ(perms.channel_grants.size(), 1u);
    const auto& g = perms.channel_grants[0];
    EXPECT_TRUE(g.actions & static_cast<uint8_t>(ChannelAction::Read));
    EXPECT_TRUE(g.actions & static_cast<uint8_t>(ChannelAction::Write));
    EXPECT_FALSE(g.actions & static_cast<uint8_t>(ChannelAction::Route));
}

TEST(AuthConfigChannels, ParseMcpSseFields) {
    nlohmann::json j = {
        {"receive_client_notification", true},
        {"receive_progress", true},
        {"receive_cancelled", false},
        {"receive_roots_changed", true},
        {"mcp_subscribe_quota", 42},
    };
    auto perms = AuthConfig::parse_role_permissions(j);
    EXPECT_TRUE(perms.receive_client_notification);
    EXPECT_TRUE(perms.receive_progress);
    EXPECT_FALSE(perms.receive_cancelled);  // overriding the `= true` default
    EXPECT_TRUE(perms.receive_roots_changed);
    EXPECT_EQ(perms.mcp_subscribe_quota, 42);
}

TEST(AuthConfigChannels, DefaultsPreservedWhenFieldsAbsent) {
    nlohmann::json j = nlohmann::json::object();
    auto perms = AuthConfig::parse_role_permissions(j);
    EXPECT_FALSE(perms.channels_enabled);
    EXPECT_TRUE(perms.channel_grants.empty());
    EXPECT_EQ(perms.channel_publish_quota, 1000);
    EXPECT_EQ(perms.channel_subscribe_quota, 10);
    EXPECT_FALSE(perms.channels_route_admin);
    EXPECT_FALSE(perms.channels_network_sink);
    EXPECT_FALSE(perms.receive_client_notification);
    EXPECT_TRUE(perms.receive_cancelled);  // struct default is true
    EXPECT_EQ(perms.mcp_subscribe_quota, 10);
}

TEST(AuthConfigChannels, RolesToJsonRoundTrip) {
    RolePermissions admin;
    admin.channels_enabled = true;
    admin.channels_route_admin = true;
    admin.channels_network_sink = true;
    admin.channel_publish_quota = 5000;
    admin.channel_subscribe_quota = 20;
    admin.receive_client_notification = true;
    admin.receive_progress = true;
    admin.receive_roots_changed = true;
    admin.mcp_subscribe_quota = 25;

    ChannelGrant g;
    g.pattern = "etil.test.**";
    g.actions = static_cast<uint8_t>(ChannelAction::Read) |
                static_cast<uint8_t>(ChannelAction::Write);
    g.effect = ChannelGrant::Effect::Allow;
    admin.channel_grants.push_back(g);

    AuthConfig cfg;
    cfg.default_role = "admin";
    cfg.roles["admin"] = admin;

    auto j = cfg.roles_to_json();
    auto rt_json = j["roles"]["admin"];
    auto rt_perms = AuthConfig::parse_role_permissions(rt_json);

    EXPECT_TRUE(rt_perms.channels_enabled);
    EXPECT_TRUE(rt_perms.channels_route_admin);
    EXPECT_TRUE(rt_perms.channels_network_sink);
    EXPECT_EQ(rt_perms.channel_publish_quota, 5000);
    EXPECT_EQ(rt_perms.channel_subscribe_quota, 20);
    EXPECT_TRUE(rt_perms.receive_client_notification);
    EXPECT_TRUE(rt_perms.receive_progress);
    EXPECT_TRUE(rt_perms.receive_roots_changed);
    EXPECT_EQ(rt_perms.mcp_subscribe_quota, 25);
    ASSERT_EQ(rt_perms.channel_grants.size(), 1u);
    EXPECT_EQ(rt_perms.channel_grants[0].pattern, "etil.test.**");
    EXPECT_EQ(rt_perms.channel_grants[0].effect,
              ChannelGrant::Effect::Allow);
    EXPECT_TRUE(rt_perms.channel_grants[0].actions &
                static_cast<uint8_t>(ChannelAction::Read));
    EXPECT_TRUE(rt_perms.channel_grants[0].actions &
                static_cast<uint8_t>(ChannelAction::Write));
}

#endif  // ETIL_JWT_ENABLED
