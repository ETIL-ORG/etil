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

// --- Phase 0: TBBP state initialization ---

TEST_F(BridgeMapTest, NewEdgeHasDefaultWeight) {
    const auto& edges = map.conversions_from(T::Integer);
    ASSERT_FALSE(edges.empty());
    for (const auto& e : edges) {
        EXPECT_DOUBLE_EQ(e.weight, 1.0);
        EXPECT_EQ(e.selections, 0u);
        EXPECT_EQ(e.successes, 0u);
    }
}

TEST_F(BridgeMapTest, AllEdgesHaveDefaultWeight) {
    // Check every edge in the graph has weight 1.0 and zero counters
    using Type = etil::core::TypeSignature::Type;
    for (int t = 0; t < static_cast<int>(Type::Custom); ++t) {
        auto from = static_cast<Type>(t);
        const auto& edges = map.conversions_from(from);
        for (const auto& e : edges) {
            EXPECT_DOUBLE_EQ(e.weight, 1.0);
            EXPECT_EQ(e.selections, 0u);
            EXPECT_EQ(e.successes, 0u);
        }
    }
}

TEST_F(BridgeMapTest, TbbpEnabledDefaultTrue) {
    BridgeMap fresh;
    EXPECT_TRUE(fresh.tbbp_enabled());
}

TEST_F(BridgeMapTest, TbbpEnabledRoundTrip) {
    BridgeMap fresh;
    fresh.set_tbbp_enabled(false);
    EXPECT_FALSE(fresh.tbbp_enabled());
    fresh.set_tbbp_enabled(true);
    EXPECT_TRUE(fresh.tbbp_enabled());
}

// --- Phase 1: Weighted path selection ---

TEST_F(BridgeMapTest, SetEdgeWeightFound) {
    EXPECT_TRUE(map.set_edge_weight(T::Integer, T::Float, "int->float", 2.5));
    const auto& edges = map.conversions_from(T::Integer);
    for (const auto& e : edges) {
        if (e.to == T::Float && e.word == "int->float") {
            EXPECT_DOUBLE_EQ(e.weight, 2.5);
            return;
        }
    }
    FAIL() << "edge not found after weight update";
}

TEST_F(BridgeMapTest, SetEdgeWeightNotFound) {
    EXPECT_FALSE(map.set_edge_weight(T::Integer, T::Float, "nonexistent", 2.5));
    EXPECT_FALSE(map.set_edge_weight(T::Unknown, T::Integer, "foo", 1.0));
}

TEST_F(BridgeMapTest, SelectPathDisabledFallsThrough) {
    // With TBBP disabled, select_path == find_path (deterministic BFS)
    map.set_tbbp_enabled(false);
    for (int i = 0; i < 10; ++i) {
        auto select_result = map.select_path(T::Integer, T::Float);
        auto find_result = map.find_path(T::Integer, T::Float);
        EXPECT_EQ(select_result, find_result);
    }
}

TEST_F(BridgeMapTest, SelectPathSingleCandidate) {
    // Integer → Float has only one edge: int->float
    auto result = map.select_path(T::Integer, T::Float);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "int->float");
}

TEST_F(BridgeMapTest, SelectPathMultipleEqualWeights) {
    // Matrix → Float has 4 candidates: mat-norm, mat-trace, mat-mean, mat-sum
    // With equal weights (all 1.0), selection should be roughly uniform
    map.set_rng_seed(42);
    std::map<std::string, int> counts;
    for (int i = 0; i < 400; ++i) {
        auto r = map.select_path(T::Matrix, T::Float);
        ASSERT_EQ(r.size(), 1u);
        counts[r[0]]++;
    }
    // All 4 should be selected; each should be ~100 ± noise (chi-squared)
    EXPECT_EQ(counts.size(), 4u);
    for (const auto& [name, count] : counts) {
        EXPECT_GT(count, 50);
        EXPECT_LT(count, 150);
    }
}

TEST_F(BridgeMapTest, SelectPathSkewedWeights) {
    // Make mat-norm heavily weighted, others at floor
    map.set_edge_weight(T::Matrix, T::Float, "mat-norm", 10.0);
    map.set_edge_weight(T::Matrix, T::Float, "mat-trace", 0.1);
    map.set_edge_weight(T::Matrix, T::Float, "mat-mean", 0.1);
    map.set_edge_weight(T::Matrix, T::Float, "mat-sum", 0.1);
    map.set_rng_seed(42);
    int mat_norm_count = 0;
    for (int i = 0; i < 100; ++i) {
        auto r = map.select_path(T::Matrix, T::Float);
        if (r.size() == 1 && r[0] == "mat-norm") mat_norm_count++;
    }
    // mat-norm should dominate — expect ≥80 out of 100
    EXPECT_GE(mat_norm_count, 80);
}

TEST_F(BridgeMapTest, SelectPathTwoHop) {
    // Array → Float requires 2 hops. Multiple paths exist:
    //   array-length (Array→Integer) + int->float (Integer→Float)
    //   array->mat (Array→Matrix) + mat-{norm,trace,mean,sum} (Matrix→Float)
    // select_path picks any of them via weighted-random.
    map.set_rng_seed(42);
    auto r = map.select_path(T::Array, T::Float);
    ASSERT_EQ(r.size(), 2u);
    // First hop must be from Array; valid options: array-length, array->mat
    EXPECT_TRUE(r[0] == "array-length" || r[0] == "array->mat");
    // Second hop must end at Float; valid options depend on first hop
    if (r[0] == "array-length") {
        EXPECT_EQ(r[1], "int->float");
    } else {
        EXPECT_TRUE(r[1] == "mat-norm" || r[1] == "mat-trace" ||
                    r[1] == "mat-mean" || r[1] == "mat-sum");
    }
}

TEST_F(BridgeMapTest, SelectPathTwoHopSkewedWeights) {
    // Boost array-length and int->float; depress everything else
    map.set_edge_weight(T::Array, T::Integer, "array-length", 10.0);
    map.set_edge_weight(T::Integer, T::Float, "int->float", 10.0);
    map.set_edge_weight(T::Array, T::Matrix, "array->mat", 0.01);
    map.set_rng_seed(42);
    int via_integer = 0;
    for (int i = 0; i < 100; ++i) {
        auto r = map.select_path(T::Array, T::Float);
        if (r.size() == 2 && r[0] == "array-length" && r[1] == "int->float")
            via_integer++;
    }
    // Should dominate — via integer path has 10*10=100, others have 0.01*(1..1)=~0.04
    EXPECT_GE(via_integer, 90);
}

TEST_F(BridgeMapTest, SelectPathEmptyNoRoute) {
    // Observable has no conversions
    auto r = map.select_path(T::Observable, T::Integer);
    EXPECT_TRUE(r.empty());
}

TEST_F(BridgeMapTest, SelectPathSameTypeEmpty) {
    auto r = map.select_path(T::Integer, T::Integer);
    EXPECT_TRUE(r.empty());
}

// --- Phase 2: Per-mutation tracking and EMA update ---

// Helper: get the weight of a specific edge
static double weight_of(const BridgeMap& m, T from, T to, const std::string& word) {
    const auto& edges = m.conversions_from(from);
    for (const auto& e : edges) {
        if (e.to == to && e.word == word) return e.weight;
    }
    return -1.0;
}

// Helper: get the selections count of a specific edge
static uint64_t selections_of(const BridgeMap& m, T from, T to, const std::string& word) {
    const auto& edges = m.conversions_from(from);
    for (const auto& e : edges) {
        if (e.to == to && e.word == word) return e.selections;
    }
    return 0;
}

TEST_F(BridgeMapTest, SelectionsIncrementedOnSelectPath) {
    map.set_rng_seed(42);
    // Integer → Float has one edge (int->float)
    EXPECT_EQ(selections_of(map, T::Integer, T::Float, "int->float"), 0u);
    map.select_path(T::Integer, T::Float);
    EXPECT_EQ(selections_of(map, T::Integer, T::Float, "int->float"), 1u);
    map.select_path(T::Integer, T::Float);
    EXPECT_EQ(selections_of(map, T::Integer, T::Float, "int->float"), 2u);
}

TEST_F(BridgeMapTest, EndMutationRewardRaisesWeight) {
    double initial = weight_of(map, T::Integer, T::Float, "int->float");
    EXPECT_DOUBLE_EQ(initial, 1.0);

    map.begin_mutation();
    map.select_path(T::Integer, T::Float);
    map.end_mutation(1.0);  // positive reward

    double after = weight_of(map, T::Integer, T::Float, "int->float");
    // EMA: (1-0.1)*1.0 + 0.1*1.0 = 1.0 → no change (already at 1.0)
    EXPECT_DOUBLE_EQ(after, 1.0);
}

TEST_F(BridgeMapTest, EndMutationNoRewardLowersWeight) {
    map.begin_mutation();
    map.select_path(T::Integer, T::Float);
    map.end_mutation(0.0);  // no reward

    double after = weight_of(map, T::Integer, T::Float, "int->float");
    // EMA: (1-0.1)*1.0 + 0.1*0.0 = 0.9
    EXPECT_NEAR(after, 0.9, 1e-9);
}

TEST_F(BridgeMapTest, EMAConvergesTowardReward) {
    // Repeatedly reward=1.0 → weight converges toward 1.0
    // Starting from floor-ish weight
    map.set_edge_weight(T::Integer, T::Float, "int->float", 0.1);
    for (int i = 0; i < 50; ++i) {
        map.begin_mutation();
        map.select_path(T::Integer, T::Float);
        map.end_mutation(1.0);
    }
    double final = weight_of(map, T::Integer, T::Float, "int->float");
    EXPECT_GT(final, 0.9);  // should have climbed close to 1.0
}

TEST_F(BridgeMapTest, WeightFloorEnforced) {
    // Repeatedly reward=0.0 → weight should bottom out at min_weight (0.05)
    for (int i = 0; i < 200; ++i) {
        map.begin_mutation();
        map.select_path(T::Integer, T::Float);
        map.end_mutation(0.0);
    }
    double final = weight_of(map, T::Integer, T::Float, "int->float");
    EXPECT_DOUBLE_EQ(final, 0.05);  // exactly at floor
}

TEST_F(BridgeMapTest, SuccessesCounterIncrements) {
    map.begin_mutation();
    map.select_path(T::Integer, T::Float);
    map.end_mutation(1.0);
    map.begin_mutation();
    map.select_path(T::Integer, T::Float);
    map.end_mutation(0.0);
    map.begin_mutation();
    map.select_path(T::Integer, T::Float);
    map.end_mutation(1.0);

    const auto& edges = map.conversions_from(T::Integer);
    for (const auto& e : edges) {
        if (e.to == T::Float && e.word == "int->float") {
            EXPECT_EQ(e.successes, 2u);  // 2 rewards of 1.0
            EXPECT_EQ(e.selections, 3u);
            return;
        }
    }
    FAIL();
}

TEST_F(BridgeMapTest, EndMutationWithoutBeginIsSafe) {
    // Calling end_mutation without begin_mutation should be a no-op
    map.end_mutation(1.0);
    double w = weight_of(map, T::Integer, T::Float, "int->float");
    EXPECT_DOUBLE_EQ(w, 1.0);
}

TEST_F(BridgeMapTest, BeginMutationClearsPreviousUsages) {
    map.select_path(T::Integer, T::Float);  // usage recorded implicitly
    map.begin_mutation();  // should clear
    // end_mutation with reward=1.0 should not affect any edge since nothing
    // was recorded after begin_mutation
    map.end_mutation(0.0);
    double w = weight_of(map, T::Integer, T::Float, "int->float");
    EXPECT_DOUBLE_EQ(w, 1.0);  // unchanged
}

TEST_F(BridgeMapTest, TwoHopEndMutationUpdatesBothEdges) {
    // Array → Float via 2 hops; both edges should be updated
    map.begin_mutation();
    auto r = map.select_path(T::Array, T::Float);
    ASSERT_EQ(r.size(), 2u);
    map.end_mutation(0.0);

    // The two edges in the path should have weight 0.9 now
    double w1 = weight_of(map, T::Array, r[0] == "array-length" ? T::Integer : T::Matrix, r[0]);
    // Second edge: its "from" is the first edge's "to"
    T mid = (r[0] == "array-length") ? T::Integer : T::Matrix;
    double w2 = weight_of(map, mid, T::Float, r[1]);
    EXPECT_NEAR(w1, 0.9, 1e-9);
    EXPECT_NEAR(w2, 0.9, 1e-9);
}

TEST_F(BridgeMapTest, CustomAlphaConfigured) {
    map.set_alpha(0.5);
    EXPECT_DOUBLE_EQ(map.alpha(), 0.5);
    map.begin_mutation();
    map.select_path(T::Integer, T::Float);
    map.end_mutation(0.0);
    // (1-0.5)*1.0 + 0.5*0.0 = 0.5
    double after = weight_of(map, T::Integer, T::Float, "int->float");
    EXPECT_NEAR(after, 0.5, 1e-9);
}

TEST_F(BridgeMapTest, CustomMinWeightConfigured) {
    map.set_min_weight(0.2);
    EXPECT_DOUBLE_EQ(map.min_weight(), 0.2);
    for (int i = 0; i < 200; ++i) {
        map.begin_mutation();
        map.select_path(T::Integer, T::Float);
        map.end_mutation(0.0);
    }
    EXPECT_DOUBLE_EQ(weight_of(map, T::Integer, T::Float, "int->float"), 0.2);
}

TEST_F(BridgeMapTest, TbbpDisabledNoStateChange) {
    map.set_tbbp_enabled(false);
    for (int i = 0; i < 50; ++i) {
        map.begin_mutation();
        map.select_path(T::Integer, T::Float);
        map.end_mutation(0.0);
    }
    // Weight unchanged, counters unchanged
    EXPECT_DOUBLE_EQ(weight_of(map, T::Integer, T::Float, "int->float"), 1.0);
    EXPECT_EQ(selections_of(map, T::Integer, T::Float, "int->float"), 0u);
}

