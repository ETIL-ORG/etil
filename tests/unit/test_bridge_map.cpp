// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/bridge_map.hpp"
#include <gtest/gtest.h>

using namespace etil::evolution;
using T = etil::core::TypeSignature::Type;

class BridgeMapTest : public ::testing::Test {
protected:
    BridgeMap map;

    void SetUp() override {
        // Build the standard 22-edge bridge map matching evolution.til
        map.add(T::Array,     T::Integer,   "array-length");
        map.add(T::Array,     T::String,    "sjoin");
        map.add(T::Array,     T::Matrix,    "array->mat");
        map.add(T::String,    T::Integer,   "slength");
        map.add(T::String,    T::Integer,   "string->number");
        map.add(T::String,    T::Array,     "ssplit");
        map.add(T::String,    T::ByteArray, "string->bytes");
        map.add(T::Integer,   T::Float,     "int->float");
        map.add(T::Integer,   T::String,    "number->string");
        map.add(T::Float,     T::Integer,   "float->int");
        map.add(T::Float,     T::String,    "number->string");
        map.add(T::Matrix,    T::Float,     "mat-norm");
        map.add(T::Matrix,    T::Float,     "mat-trace");
        map.add(T::Matrix,    T::Float,     "mat-mean");
        map.add(T::Matrix,    T::Float,     "mat-sum");
        map.add(T::Matrix,    T::Integer,   "mat-rows");
        map.add(T::Matrix,    T::Integer,   "mat-cols");
        map.add(T::Matrix,    T::Array,     "mat->array");
        map.add(T::ByteArray, T::String,    "bytes->string");
        map.add(T::ByteArray, T::Integer,   "bytes-length");
        map.add(T::Json,      T::String,    "json-dump");
        map.add(T::Json,      T::Array,     "json->array");
        map.add(T::Json,      T::Map,       "json->map");
        map.add(T::Map,       T::Json,      "map->json");
        map.add(T::Map,       T::Array,     "map-keys");
        map.add(T::Map,       T::Array,     "map-values");
        map.add(T::Map,       T::Integer,   "map-length");
        map.add(T::Matrix,    T::Json,      "mat->json");
        map.finalize();
    }
};

TEST_F(BridgeMapTest, EdgeCount) {
    EXPECT_EQ(map.size(), 28u);  // 28 edges (some types have multiple conversion words)
}

TEST_F(BridgeMapTest, SourceTypeCount) {
    EXPECT_EQ(map.source_type_count(), 8u);  // Array, String, Integer, Float, Matrix, ByteArray, Json, Map
}

TEST_F(BridgeMapTest, FindBridgeDirect) {
    auto result = map.find_bridge(T::Integer, T::Float);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "int->float");
}

TEST_F(BridgeMapTest, FindBridgeNoSelfLoop) {
    auto result = map.find_bridge(T::Integer, T::Integer);
    EXPECT_TRUE(result.empty());
}

TEST_F(BridgeMapTest, FindBridgeNoDirectEdge) {
    auto result = map.find_bridge(T::Array, T::Float);
    EXPECT_TRUE(result.empty());
}

TEST_F(BridgeMapTest, FindBridgeMultipleWords) {
    // Matrix → Float has 4 conversion words
    auto result = map.find_bridge(T::Matrix, T::Float);
    EXPECT_EQ(result.size(), 4u);
}

TEST_F(BridgeMapTest, FindPathTwoHop) {
    // Array → Float via array-length (Array→Integer) + int->float (Integer→Float)
    auto path = map.find_path(T::Array, T::Float, 2);
    ASSERT_EQ(path.size(), 2u);
    EXPECT_EQ(path[0], "array-length");
    EXPECT_EQ(path[1], "int->float");
}

TEST_F(BridgeMapTest, FindPathNoUsefulPath) {
    auto path = map.find_path(T::Integer, T::Integer, 2);
    EXPECT_TRUE(path.empty());
}

TEST_F(BridgeMapTest, FindPathSingleHop) {
    auto path = map.find_path(T::Integer, T::Float, 2);
    ASSERT_EQ(path.size(), 1u);
    EXPECT_EQ(path[0], "int->float");
}

TEST_F(BridgeMapTest, ConversionsFromMatrix) {
    const auto& edges = map.conversions_from(T::Matrix);
    EXPECT_GE(edges.size(), 4u);  // at least mat-norm, mat-trace, mat-mean, mat-sum
    // Check that Float, Integer, Array, Json targets are present
    bool has_float = false, has_int = false, has_array = false, has_json = false;
    for (const auto& e : edges) {
        if (e.to == T::Float)   has_float = true;
        if (e.to == T::Integer) has_int = true;
        if (e.to == T::Array)   has_array = true;
        if (e.to == T::Json)    has_json = true;
    }
    EXPECT_TRUE(has_float);
    EXPECT_TRUE(has_int);
    EXPECT_TRUE(has_array);
    EXPECT_TRUE(has_json);
}

TEST_F(BridgeMapTest, HasConversions) {
    EXPECT_TRUE(map.has_conversions(T::Integer));
    EXPECT_TRUE(map.has_conversions(T::Matrix));
    EXPECT_FALSE(map.has_conversions(T::Unknown));
    EXPECT_FALSE(map.has_conversions(T::Observable));
    EXPECT_FALSE(map.has_conversions(T::Boolean));
}

TEST_F(BridgeMapTest, SummaryNotEmpty) {
    auto s = map.summary();
    EXPECT_FALSE(s.empty());
    // Should mention edge count
    EXPECT_NE(s.find("28 edges"), std::string::npos);
}

// --- parse_sig_type ---

TEST_F(BridgeMapTest, ParseSigTypeKnown) {
    EXPECT_EQ(BridgeMap::parse_sig_type("integer"),   T::Integer);
    EXPECT_EQ(BridgeMap::parse_sig_type("float"),     T::Float);
    EXPECT_EQ(BridgeMap::parse_sig_type("boolean"),   T::Boolean);
    EXPECT_EQ(BridgeMap::parse_sig_type("string"),    T::String);
    EXPECT_EQ(BridgeMap::parse_sig_type("array"),     T::Array);
    EXPECT_EQ(BridgeMap::parse_sig_type("bytearray"), T::ByteArray);
    EXPECT_EQ(BridgeMap::parse_sig_type("map"),       T::Map);
    EXPECT_EQ(BridgeMap::parse_sig_type("json"),      T::Json);
    EXPECT_EQ(BridgeMap::parse_sig_type("matrix"),    T::Matrix);
    EXPECT_EQ(BridgeMap::parse_sig_type("observable"),T::Observable);
    EXPECT_EQ(BridgeMap::parse_sig_type("xt"),        T::Xt);
    EXPECT_EQ(BridgeMap::parse_sig_type("dataref"),   T::DataRef);
}

TEST_F(BridgeMapTest, ParseSigTypeUnknown) {
    EXPECT_EQ(BridgeMap::parse_sig_type("garbage"),  T::Unknown);
    EXPECT_EQ(BridgeMap::parse_sig_type(""),         T::Unknown);
    EXPECT_EQ(BridgeMap::parse_sig_type("Integer"),  T::Unknown);  // case-sensitive
}

// --- type_name ---

TEST_F(BridgeMapTest, TypeNameRoundTrip) {
    EXPECT_STREQ(BridgeMap::type_name(T::Integer),  "Integer");
    EXPECT_STREQ(BridgeMap::type_name(T::Float),    "Float");
    EXPECT_STREQ(BridgeMap::type_name(T::Boolean),  "Boolean");
    EXPECT_STREQ(BridgeMap::type_name(T::String),   "String");
    EXPECT_STREQ(BridgeMap::type_name(T::Unknown),  "Unknown");
}

