// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/sinks.hpp"
#include "etil/manifold/transforms.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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
// TestCaptureSink
// ---------------------------------------------------------------------------

TEST(Sinks, TestCaptureCollectsInOrder) {
    auto s = make_test_capture_sink();
    s->accept(make_msg("a"));
    s->accept(make_msg("b"));
    s->accept(make_msg("c"));
    auto got = s->captured();
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0].channel, "a");
    EXPECT_EQ(got[2].channel, "c");
    s->clear();
    EXPECT_EQ(s->size(), 0u);
}

// ---------------------------------------------------------------------------
// RingBufferSink — drop_first (default)
// ---------------------------------------------------------------------------

TEST(Sinks, RingDropFirstEvictsOldest) {
    auto s = make_ring_buffer_sink(3, /*drop_first=*/true);
    for (int i = 0; i < 5; ++i) {
        s->accept(make_msg("c" + std::to_string(i)));
    }
    auto snap = s->snapshot();
    ASSERT_EQ(snap.size(), 3u);
    EXPECT_EQ(snap[0].channel, "c2");  // oldest after eviction
    EXPECT_EQ(snap[2].channel, "c4");
    EXPECT_EQ(s->dropped_count(), 2u);
}

TEST(Sinks, RingDropLastRefusesNew) {
    auto s = make_ring_buffer_sink(2, /*drop_first=*/false);
    s->accept(make_msg("keep1"));
    s->accept(make_msg("keep2"));
    s->accept(make_msg("reject"));
    auto snap = s->snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_EQ(snap[0].channel, "keep1");
    EXPECT_EQ(snap[1].channel, "keep2");
    EXPECT_EQ(s->dropped_count(), 1u);
}

// ---------------------------------------------------------------------------
// NullSink — discards silently
// ---------------------------------------------------------------------------

TEST(Sinks, NullSinkAcceptsAndDiscards) {
    auto s = make_null_sink();
    s->accept(make_msg("whatever"));
    // If we didn't crash, we're good.
    SUCCEED();
}

// ---------------------------------------------------------------------------
// FileSink — writes one line per message
// ---------------------------------------------------------------------------

TEST(Sinks, FileSinkWritesOneLinePerAccept) {
    auto tmp = std::filesystem::temp_directory_path() /
               ("etil_manifold_file_sink_" +
                std::to_string(::getpid()) + ".log");
    {
        auto s = make_file_sink(tmp.string());
        s->accept(make_msg("ch.one", "alpha"));
        s->accept(make_msg("ch.two", "beta"));
        s->flush();
    }
    std::ifstream in(tmp);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("alpha"), std::string::npos);
    EXPECT_NE(content.find("beta"), std::string::npos);
    EXPECT_NE(content.find("ch.one"), std::string::npos);
    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------

TEST(Transforms, AnnotatorAddsTag) {
    auto t = make_tag_annotator("k", "v");
    auto out = t->apply(make_msg("x"));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].tags.at("k"), "v");
}

TEST(Transforms, FormatterStringifiesNonString) {
    auto t = make_formatter();
    Message m;
    m.channel = "x";
    m.payload = 42;  // int — not std::string
    m.payload_type = std::type_index(typeid(int));
    auto out = t->apply(std::move(m));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].payload_type, std::type_index(typeid(std::string)));
}

TEST(Transforms, ChannelFilterDropsNonMatches) {
    auto t = make_channel_filter("etil.keep.**");
    EXPECT_EQ(t->apply(make_msg("etil.keep.x")).size(), 1u);
    EXPECT_EQ(t->apply(make_msg("etil.drop.x")).size(), 0u);
}

TEST(Transforms, TagFilterRequiresExactMatch) {
    auto t = make_tag_filter("color", "red");
    auto m1 = make_msg("x");
    m1.tags["color"] = "red";
    auto m2 = make_msg("x");
    m2.tags["color"] = "blue";
    auto m3 = make_msg("x");  // no color tag
    EXPECT_EQ(t->apply(std::move(m1)).size(), 1u);
    EXPECT_EQ(t->apply(std::move(m2)).size(), 0u);
    EXPECT_EQ(t->apply(std::move(m3)).size(), 0u);
}

TEST(Transforms, SamplerKeepsOneInN) {
    auto t = make_sampler(3);
    int kept = 0;
    for (int i = 0; i < 12; ++i) {
        if (!t->apply(make_msg("x")).empty()) ++kept;
    }
    EXPECT_EQ(kept, 4);  // 0, 3, 6, 9
}

TEST(Transforms, FanOutReplicatesOntoExtraChannels) {
    auto t = make_fan_out({"etil.b", "etil.c"});
    auto out = t->apply(make_msg("etil.a"));
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].channel, "etil.a");
    EXPECT_EQ(out[1].channel, "etil.b");
    EXPECT_EQ(out[2].channel, "etil.c");
}

TEST(Transforms, JsonEncoderProducesParseableJson) {
    auto t = make_json_encoder();
    Message m;
    m.channel = "etil.x";
    m.payload = std::string("payload-text");
    m.payload_type = std::type_index(typeid(std::string));
    m.tags["level"] = "info";
    auto out = t->apply(std::move(m));
    ASSERT_EQ(out.size(), 1u);
    auto j = nlohmann::json::parse(std::any_cast<std::string>(out[0].payload));
    EXPECT_EQ(j["channel"], "etil.x");
    EXPECT_EQ(j["payload"], "payload-text");
    EXPECT_EQ(j["tags"]["level"], "info");
}
