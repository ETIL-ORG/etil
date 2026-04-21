// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/rbac.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"
#include "etil/mcp/role_permissions.hpp"

#include <gtest/gtest.h>

using namespace etil::manifold;
using etil::mcp::RolePermissions;

namespace {

RolePermissions master_off() {
    RolePermissions r;
    r.channels_enabled = false;
    return r;
}

RolePermissions master_on_no_grants() {
    RolePermissions r;
    r.channels_enabled = true;
    return r;
}

RolePermissions role_with(std::vector<ChannelGrant> grants) {
    RolePermissions r;
    r.channels_enabled = true;
    r.channel_grants = std::move(grants);
    return r;
}

Message make_msg(std::string channel, std::string payload = "") {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::move(payload);
    m.payload_type = std::type_index(typeid(std::string));
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// evaluate_access — decision procedure
// ---------------------------------------------------------------------------

TEST(RBAC, StandaloneAllowsEverything) {
    auto d = evaluate_access(nullptr, "etil.anywhere", ChannelAction::Write);
    EXPECT_TRUE(d.allowed);
    EXPECT_EQ(d.reason, DecisionReason::Allowed_Standalone);
}

TEST(RBAC, MasterOffBlocksEverythingExceptHardwired) {
    RolePermissions r = master_off();

    auto writeable = evaluate_access(&r, "etil.repl.stdout", ChannelAction::Write);
    EXPECT_FALSE(writeable.allowed);
    EXPECT_EQ(writeable.reason, DecisionReason::Denied_MasterOff);

    // Hard-wired audit write still allowed.
    auto audit = evaluate_access(&r, "etil.aaa.audit.channel.denied",
                                 ChannelAction::Write);
    EXPECT_TRUE(audit.allowed);
    EXPECT_EQ(audit.reason, DecisionReason::Allowed_HardWired);
}

TEST(RBAC, NoGrantsDefaultDeny) {
    RolePermissions r = master_on_no_grants();
    auto d = evaluate_access(&r, "etil.repl.stdout", ChannelAction::Write);
    EXPECT_FALSE(d.allowed);
    EXPECT_EQ(d.reason, DecisionReason::Denied_NoMatchingGrant);
}

TEST(RBAC, GrantAllowsMatchingChannel) {
    RolePermissions r = role_with({
        {"etil.evolution.**",
         static_cast<uint8_t>(ChannelAction::Read | ChannelAction::Write),
         ChannelGrant::Effect::Allow},
    });
    auto w = evaluate_access(&r, "etil.evolution.fitness", ChannelAction::Write);
    EXPECT_TRUE(w.allowed);
    EXPECT_EQ(w.reason, DecisionReason::Allowed_ByGrant);

    auto nope = evaluate_access(&r, "etil.mcp.out", ChannelAction::Write);
    EXPECT_FALSE(nope.allowed);
}

TEST(RBAC, MoreSpecificDenyOverridesBroaderAllow) {
    RolePermissions r = role_with({
        {"etil.evolution.**",      to_mask(ChannelAction::Write),
         ChannelGrant::Effect::Allow},
        {"etil.evolution.fitness", to_mask(ChannelAction::Write),
         ChannelGrant::Effect::Deny},
    });
    auto fit = evaluate_access(&r, "etil.evolution.fitness",
                               ChannelAction::Write);
    EXPECT_FALSE(fit.allowed);
    EXPECT_EQ(fit.reason, DecisionReason::Denied_ExplicitDeny);

    auto sel = evaluate_access(&r, "etil.evolution.selection",
                               ChannelAction::Write);
    EXPECT_TRUE(sel.allowed);
}

TEST(RBAC, RouteActionRequiresRouteAdmin) {
    RolePermissions r = role_with({
        {"etil.**",
         static_cast<uint8_t>(ChannelAction::Read | ChannelAction::Write
                               | ChannelAction::Route | ChannelAction::Introspect),
         ChannelGrant::Effect::Allow},
    });
    r.channels_route_admin = false;
    auto d = evaluate_access(&r, "etil.foo", ChannelAction::Route);
    EXPECT_FALSE(d.allowed);
    EXPECT_EQ(d.reason, DecisionReason::Denied_RouteAdminRequired);

    r.channels_route_admin = true;
    auto d2 = evaluate_access(&r, "etil.foo", ChannelAction::Route);
    EXPECT_TRUE(d2.allowed);
}

// ---------------------------------------------------------------------------
// Hardwired-channel table introspection
// ---------------------------------------------------------------------------

TEST(RBAC, HardwiredAuditIsWriteable) {
    EXPECT_TRUE(is_hardwired("etil.aaa.audit.channel.denied",
                             ChannelAction::Write));
    EXPECT_TRUE(is_hardwired("etil.security.alert", ChannelAction::Write));
    EXPECT_TRUE(is_hardwired("etil.logging.error", ChannelAction::Write));
    EXPECT_TRUE(is_hardwired("etil.system.bootstrap.start",
                             ChannelAction::Write));
    EXPECT_TRUE(is_hardwired("etil.system.bootstrap.start",
                             ChannelAction::Read));
}

TEST(RBAC, HardwiredAuditIsInlineDelivery) {
    EXPECT_EQ(hardwired_delivery_mode("etil.aaa.audit.channel.denied"),
              DeliveryMode::Inline);
    EXPECT_EQ(hardwired_delivery_mode("etil.security.alert"),
              DeliveryMode::Inline);
    EXPECT_EQ(hardwired_delivery_mode("etil.repl.stdout"),
              DeliveryMode::RingBuffered);  // default
}

// ---------------------------------------------------------------------------
// ChannelService integrates RBAC on publish + add_route
// ---------------------------------------------------------------------------

TEST(ChannelServiceRBAC, DeniedPublishReturnsDeniedOutcome) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.repl.stdout";
    spec.sink = capture;
    svc->add_route(std::move(spec));

    RolePermissions r = master_off();
    auto out = svc->publish(make_msg("etil.repl.stdout", "nope"), &r);
    EXPECT_FALSE(out.accepted);
    EXPECT_TRUE(out.denied_by_rbac);
    EXPECT_EQ(capture->size(), 0u);
}

TEST(ChannelServiceRBAC, HardwiredWriteBypassesMasterOff) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.aaa.audit.**";
    spec.sink = capture;
    svc->add_route(std::move(spec));

    RolePermissions r = master_off();
    auto out = svc->publish(make_msg("etil.aaa.audit.channel.denied", "forced"),
                            &r);
    EXPECT_TRUE(out.accepted);
    svc->flush_for_tests();
    EXPECT_EQ(capture->size(), 1u);
}

TEST(ChannelServiceRBAC, AddRouteDeniedWithoutRouteAdmin) {
    auto svc = make_default_channel_service();
    RolePermissions r = master_on_no_grants();
    RouteSpec spec;
    spec.channel_pattern = "etil.foo";
    spec.sink = make_null_sink();
    auto h = svc->add_route(std::move(spec), &r);
    EXPECT_FALSE(h.valid());
}

TEST(ChannelServiceRBAC, AddRouteAllowedWithRouteAdminGrant) {
    auto svc = make_default_channel_service();
    RolePermissions r = role_with({
        {"etil.foo", to_mask(ChannelAction::Route),
         ChannelGrant::Effect::Allow},
    });
    r.channels_route_admin = true;
    RouteSpec spec;
    spec.channel_pattern = "etil.foo";
    spec.sink = make_null_sink();
    auto h = svc->add_route(std::move(spec), &r);
    EXPECT_TRUE(h.valid());
}
