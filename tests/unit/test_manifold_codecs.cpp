// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Tests for the four Manifold codec transforms (json, msgpack, cbor,
// raw) and the codec_resolver that maps codec-name strings to their
// transform factories. Part of Phase 3a broker-sink scaffolding
// (docs/claude-design/20260419A §Phase 3a).

#include "etil/manifold/codec_resolver.hpp"
#include "etil/manifold/transforms.hpp"

#include <string>
#include <typeindex>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace etil::manifold;

namespace {

Message make_msg(std::string channel, std::string payload = "hello") {
    Message m;
    m.channel = std::move(channel);
    m.payload = std::move(payload);
    m.payload_type = std::type_index(typeid(std::string));
    m.tags["level"] = "info";
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// resolve_codec — name → transform
// ---------------------------------------------------------------------------

TEST(CodecResolver, KnownNamesReturnNonNull) {
    EXPECT_NE(resolve_codec("json"), nullptr);
    EXPECT_NE(resolve_codec("msgpack"), nullptr);
    EXPECT_NE(resolve_codec("cbor"), nullptr);
    EXPECT_NE(resolve_codec("raw"), nullptr);
}

TEST(CodecResolver, EmptyStringAliasesToJson) {
    EXPECT_NE(resolve_codec(""), nullptr);
    // Behavioral parity with "json": both encode to std::string.
    auto a = resolve_codec("");
    auto out = a->apply(make_msg("etil.x"));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].payload_type, std::type_index(typeid(std::string)));
}

TEST(CodecResolver, UnknownCodecReturnsNull) {
    EXPECT_EQ(resolve_codec("protobuf"), nullptr);
    EXPECT_EQ(resolve_codec("avro"), nullptr);
    EXPECT_EQ(resolve_codec("JSON"), nullptr);  // case-sensitive
}

// ---------------------------------------------------------------------------
// MessagePack encoder — round-trip via nlohmann::json
// ---------------------------------------------------------------------------

TEST(MsgpackEncoder, ProducesByteVectorPayload) {
    auto t = make_msgpack_encoder();
    auto out = t->apply(make_msg("etil.evolution.fitness", "payload-text"));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].payload_type,
              std::type_index(typeid(std::vector<uint8_t>)));
    const auto& bytes = std::any_cast<const std::vector<uint8_t>&>(out[0].payload);
    EXPECT_FALSE(bytes.empty());
}

TEST(MsgpackEncoder, RoundTripPreservesEnvelopeFields) {
    auto t = make_msgpack_encoder();
    auto out = t->apply(make_msg("etil.x", "alpha"));
    const auto& bytes = std::any_cast<const std::vector<uint8_t>&>(out[0].payload);
    auto decoded = nlohmann::json::from_msgpack(bytes);
    EXPECT_EQ(decoded["channel"], "etil.x");
    EXPECT_EQ(decoded["payload"], "alpha");
    EXPECT_EQ(decoded["tags"]["level"], "info");
    EXPECT_TRUE(decoded.contains("origin"));
    EXPECT_TRUE(decoded.contains("ts_us"));
}

// ---------------------------------------------------------------------------
// CBOR encoder — round-trip via nlohmann::json
// ---------------------------------------------------------------------------

TEST(CborEncoder, ProducesByteVectorPayload) {
    auto t = make_cbor_encoder();
    auto out = t->apply(make_msg("etil.health.tick"));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].payload_type,
              std::type_index(typeid(std::vector<uint8_t>)));
}

TEST(CborEncoder, RoundTripPreservesPayload) {
    auto t = make_cbor_encoder();
    auto out = t->apply(make_msg("etil.x", "beta"));
    const auto& bytes = std::any_cast<const std::vector<uint8_t>&>(out[0].payload);
    auto decoded = nlohmann::json::from_cbor(bytes);
    EXPECT_EQ(decoded["channel"], "etil.x");
    EXPECT_EQ(decoded["payload"], "beta");
}

// ---------------------------------------------------------------------------
// Raw pass-through — type-check on input
// ---------------------------------------------------------------------------

TEST(RawPassthrough, ForwardsByteVectorUnchanged) {
    auto t = make_raw_passthrough();
    Message m;
    m.channel = "etil.raw";
    std::vector<uint8_t> body{0xDE, 0xAD, 0xBE, 0xEF};
    m.payload = body;
    m.payload_type = std::type_index(typeid(std::vector<uint8_t>));
    auto out = t->apply(std::move(m));
    ASSERT_EQ(out.size(), 1u);
    const auto& got = std::any_cast<const std::vector<uint8_t>&>(out[0].payload);
    EXPECT_EQ(got, body);
}

TEST(RawPassthrough, DropsNonByteVectorPayload) {
    auto t = make_raw_passthrough();
    Message m;
    m.channel = "etil.raw";
    m.payload = std::string("not-bytes");
    m.payload_type = std::type_index(typeid(std::string));
    auto out = t->apply(std::move(m));
    EXPECT_EQ(out.size(), 0u);
}
