// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/service.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/sinks.hpp"
#include "etil/manifold/transforms.hpp"
#include "etil/manifold/channel_name.hpp"

#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace etil::manifold;

namespace {

Message make_msg(std::string channel, std::string payload = "") {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::move(payload);
    m.payload_type = std::type_index(typeid(std::string));
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// Channel name grammar
// ---------------------------------------------------------------------------

TEST(ChannelName, ValidatesLiteralNames) {
    EXPECT_TRUE(validate_channel_name("etil.mcp.out.notification.system"));
    EXPECT_TRUE(validate_channel_name("a"));
    EXPECT_TRUE(validate_channel_name("etil.foo.bar.2"));
    EXPECT_FALSE(validate_channel_name(""));
    EXPECT_FALSE(validate_channel_name(".leading"));
    EXPECT_FALSE(validate_channel_name("trailing."));
    EXPECT_FALSE(validate_channel_name("double..dot"));
    EXPECT_FALSE(validate_channel_name("etil.*.out"));  // wildcards not allowed in names
}

TEST(ChannelName, ValidatesPatterns) {
    EXPECT_TRUE(validate_pattern("etil.mcp.**"));
    EXPECT_TRUE(validate_pattern("etil.*.out"));
    EXPECT_TRUE(validate_pattern("**"));
    EXPECT_FALSE(validate_pattern("etil.*bad"));  // wildcard inside segment
}

TEST(ChannelName, MatchesExact) {
    EXPECT_TRUE(channel_matches("etil.mcp.out", "etil.mcp.out"));
    EXPECT_FALSE(channel_matches("etil.mcp.out", "etil.mcp.in"));
}

TEST(ChannelName, MatchesSingleWildcard) {
    EXPECT_TRUE(channel_matches("etil.*.out", "etil.mcp.out"));
    EXPECT_FALSE(channel_matches("etil.*.out", "etil.mcp.in.out"));
}

TEST(ChannelName, MatchesDoubleWildcard) {
    EXPECT_TRUE(channel_matches("etil.mcp.**", "etil.mcp.out"));
    EXPECT_TRUE(channel_matches("etil.mcp.**", "etil.mcp.out.notification.system"));
    EXPECT_TRUE(channel_matches("etil.**", "etil.mcp"));
    EXPECT_FALSE(channel_matches("etil.mcp.**", "etil.http.get"));
}

TEST(ChannelName, SpecificityRanksLiteralHigher) {
    EXPECT_GT(pattern_specificity("etil.mcp.out"),
              pattern_specificity("etil.mcp.*"));
    EXPECT_GT(pattern_specificity("etil.mcp.*"),
              pattern_specificity("etil.mcp.**"));
}

// ---------------------------------------------------------------------------
// Origin tuple
// ---------------------------------------------------------------------------

TEST(Origin, InitPopulatesHostAndStartup) {
    shutdown_origin();
    init_origin();
    EXPECT_TRUE(origin_is_initialized());
    EXPECT_FALSE(origin_hostname().empty());
    EXPECT_GT(origin_app_startup_us(), 0);
}

TEST(Origin, SequenceCounterIsMonotonic) {
    shutdown_origin();
    init_origin();
    auto a = current_origin("s1");
    auto b = current_origin("s1");
    auto c = current_origin("s2");
    EXPECT_EQ(b.seq, a.seq + 1);
    EXPECT_EQ(c.seq, b.seq + 1);
    EXPECT_EQ(a.session_id, "s1");
    EXPECT_EQ(c.session_id, "s2");
}

TEST(Origin, HostnameOverrideForBrowser) {
    shutdown_origin();
    init_origin(OriginType::Browser, "https://etil-org.github.io");
    EXPECT_EQ(origin_hostname(), "https://etil-org.github.io");
    EXPECT_EQ(origin_type(), OriginType::Browser);
    shutdown_origin();
    init_origin();  // restore native for subsequent tests
}

// ---------------------------------------------------------------------------
// ChannelService — publish/route lifecycle
// ---------------------------------------------------------------------------

TEST(ChannelService, PublishWithoutRouteIsAccepted) {
    auto svc = make_default_channel_service();
    auto outcome = svc->publish(make_msg("etil.test.stray", "hello"));
    EXPECT_TRUE(outcome.accepted);
    EXPECT_EQ(outcome.routes_matched, 0u);
}

TEST(ChannelService, RouteAddReceivesMatchingMessages) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();

    RouteSpec spec;
    spec.channel_pattern = "etil.test.**";
    spec.sink = capture;
    auto handle = svc->add_route(std::move(spec));
    ASSERT_TRUE(handle.valid());

    svc->publish(make_msg("etil.test.a", "first"));
    svc->publish(make_msg("etil.test.nested.b", "second"));
    svc->publish(make_msg("etil.other", "ignored"));
    svc->flush_for_tests();

    EXPECT_EQ(capture->size(), 2u);
}

TEST(ChannelService, RouteRemoveStopsDelivery) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.removal";
    spec.sink = capture;
    auto handle = svc->add_route(std::move(spec));

    svc->publish(make_msg("etil.removal", "before"));
    svc->flush_for_tests();  // drain before remove so the in-flight item lands
    svc->remove_route(handle);
    svc->publish(make_msg("etil.removal", "after"));
    svc->flush_for_tests();

    EXPECT_EQ(capture->size(), 1u);
}

TEST(ChannelService, TagFilterSelectsMessages) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.tagged";
    spec.tag_filter["color"] = "red";
    spec.sink = capture;
    svc->add_route(std::move(spec));

    auto red = make_msg("etil.tagged", "r");
    red.tags["color"] = "red";
    svc->publish(std::move(red));

    auto blue = make_msg("etil.tagged", "b");
    blue.tags["color"] = "blue";
    svc->publish(std::move(blue));
    svc->flush_for_tests();

    EXPECT_EQ(capture->size(), 1u);
}

TEST(ChannelService, TransformsRunBeforeSink) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.xform";
    spec.transforms.push_back(make_tag_annotator("annotated", "yes"));
    spec.sink = capture;
    svc->add_route(std::move(spec));

    svc->publish(make_msg("etil.xform"));
    svc->flush_for_tests();
    ASSERT_EQ(capture->size(), 1u);
    auto msgs = capture->captured();
    EXPECT_EQ(msgs[0].tags.at("annotated"), "yes");
}

TEST(ChannelService, LevelFilterDropsBelowThreshold) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.lvl";
    spec.transforms.push_back(make_level_filter("warn"));
    spec.sink = capture;
    svc->add_route(std::move(spec));

    auto info = make_msg("etil.lvl", "info-line");
    info.tags["level"] = "info";
    svc->publish(std::move(info));

    auto warn = make_msg("etil.lvl", "warn-line");
    warn.tags["level"] = "warn";
    svc->publish(std::move(warn));
    svc->flush_for_tests();

    EXPECT_EQ(capture->size(), 1u);
}

TEST(ChannelService, SinkStatsTrackAccepted) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.stats";
    spec.sink = capture;
    auto handle = svc->add_route(std::move(spec));

    for (int i = 0; i < 5; ++i) {
        svc->publish(make_msg("etil.stats"));
    }
    svc->flush_for_tests();
    auto stats = svc->all_sink_stats();
    ASSERT_EQ(stats.size(), 1u);
    EXPECT_EQ(stats[0].accepted_count, 5u);
    EXPECT_EQ(stats[0].handle.id, handle.id);
}

// ---------------------------------------------------------------------------
// Thread-safety stress — concurrent publishers share one route
// ---------------------------------------------------------------------------

TEST(ChannelService, ConcurrentPublishersDeliverAllMessages) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.stress";
    spec.sink = capture;
    svc->add_route(std::move(spec));

    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([svc, t]() {
            for (int i = 0; i < kPerThread; ++i) {
                svc->publish(make_msg("etil.stress",
                                      std::to_string(t) + ":" + std::to_string(i)));
            }
        });
    }
    for (auto& th : threads) th.join();
    svc->flush_for_tests();

    EXPECT_EQ(capture->size(), static_cast<size_t>(kThreads * kPerThread));
}

// ---------------------------------------------------------------------------
// Origin sequence numbers are unique across concurrent publishers
// ---------------------------------------------------------------------------

TEST(ChannelService, OriginSeqsAreUniquePerPublish) {
    auto svc = make_default_channel_service();
    auto capture = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.seq";
    spec.sink = capture;
    svc->add_route(std::move(spec));

    constexpr int kThreads = 4;
    constexpr int kPerThread = 200;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([svc]() {
            for (int i = 0; i < kPerThread; ++i) {
                svc->publish(make_msg("etil.seq"));
            }
        });
    }
    for (auto& th : threads) th.join();
    svc->flush_for_tests();

    auto msgs = capture->captured();
    std::vector<int64_t> seqs;
    seqs.reserve(msgs.size());
    for (auto& m : msgs) seqs.push_back(m.origin.seq);
    std::sort(seqs.begin(), seqs.end());
    auto uniq_end = std::unique(seqs.begin(), seqs.end());
    EXPECT_EQ(uniq_end, seqs.end());  // all unique
}
