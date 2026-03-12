// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_observable.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::core;

class ObservablePrimitivesTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    Interpreter interp{dict, out, err};

    void SetUp() override {
        register_primitives(dict);
    }

    void run(const std::string& code) {
        out.str("");
        err.str("");
        interp.interpret_line(code);
    }

    ExecutionContext& ctx() { return interp.context(); }
};

// --- Creation ---

TEST_F(ObservablePrimitivesTest, ObsFromArray) {
    run("array-new 10 array-push 20 array-push 30 array-push obs-from");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Observable);
    opt->release();
}

TEST_F(ObservablePrimitivesTest, ObsOf) {
    run("42 obs-of");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Observable);
    opt->release();
}

TEST_F(ObservablePrimitivesTest, ObsEmpty) {
    run("obs-empty");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Observable);
    opt->release();
}

TEST_F(ObservablePrimitivesTest, ObsRange) {
    run("1 5 obs-range");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Observable);
    opt->release();
}

// --- Terminal: obs-to-array ---

TEST_F(ObservablePrimitivesTest, ObsFromToArray) {
    run("array-new 10 array-push 20 array-push 30 array-push obs-from obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Array);
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 10);
    arr->get(1, v); EXPECT_EQ(v.as_int, 20);
    arr->get(2, v); EXPECT_EQ(v.as_int, 30);
    arr->release();
}

TEST_F(ObservablePrimitivesTest, ObsRangeToArray) {
    run("1 4 obs-range obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 1);
    arr->get(1, v); EXPECT_EQ(v.as_int, 2);
    arr->get(2, v); EXPECT_EQ(v.as_int, 3);
    arr->release();
}

TEST_F(ObservablePrimitivesTest, ObsOfToArray) {
    run("99 obs-of obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 1u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 99);
    arr->release();
}

TEST_F(ObservablePrimitivesTest, ObsEmptyToArray) {
    run("obs-empty obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 0u);
    arr->release();
}

// --- Terminal: obs-count ---

TEST_F(ObservablePrimitivesTest, ObsCount) {
    run("1 10 obs-range obs-count");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 9);
}

TEST_F(ObservablePrimitivesTest, ObsCountEmpty) {
    run("obs-empty obs-count");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 0);
}

// --- Transform: obs-map ---

TEST_F(ObservablePrimitivesTest, ObsMap) {
    run(": double dup + ;");
    run("1 4 obs-range ' double obs-map obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 2);
    arr->get(1, v); EXPECT_EQ(v.as_int, 4);
    arr->get(2, v); EXPECT_EQ(v.as_int, 6);
    arr->release();
}

// --- Transform: obs-filter ---

TEST_F(ObservablePrimitivesTest, ObsFilter) {
    run(": even? 2 mod 0 = ;");
    run("1 7 obs-range ' even? obs-filter obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 2);
    arr->get(1, v); EXPECT_EQ(v.as_int, 4);
    arr->get(2, v); EXPECT_EQ(v.as_int, 6);
    arr->release();
}

TEST_F(ObservablePrimitivesTest, ObsFilterNonePass) {
    run(": never false ;");
    run("1 4 obs-range ' never obs-filter obs-count");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 0);
}

// --- Accumulate: obs-scan ---

TEST_F(ObservablePrimitivesTest, ObsScan) {
    run(": add + ;");
    run("1 4 obs-range ' add 0 obs-scan obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 1);   // 0+1
    arr->get(1, v); EXPECT_EQ(v.as_int, 3);   // 1+2
    arr->get(2, v); EXPECT_EQ(v.as_int, 6);   // 3+3
    arr->release();
}

// --- Accumulate: obs-reduce ---

TEST_F(ObservablePrimitivesTest, ObsReduce) {
    run(": add + ;");
    run("1 6 obs-range ' add 0 obs-reduce");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 15);  // 1+2+3+4+5
}

TEST_F(ObservablePrimitivesTest, ObsReduceEmpty) {
    run(": add + ;");
    run("obs-empty ' add 0 obs-reduce");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 0);  // initial value returned
}

// --- Limiting: obs-take ---

TEST_F(ObservablePrimitivesTest, ObsTake) {
    run("1 100 obs-range 5 obs-take obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 5u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 1);
    arr->get(4, v); EXPECT_EQ(v.as_int, 5);
    arr->release();
}

// --- Limiting: obs-skip ---

TEST_F(ObservablePrimitivesTest, ObsSkip) {
    run("1 6 obs-range 2 obs-skip obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 3);
    arr->get(1, v); EXPECT_EQ(v.as_int, 4);
    arr->get(2, v); EXPECT_EQ(v.as_int, 5);
    arr->release();
}

// --- Limiting: obs-distinct ---

TEST_F(ObservablePrimitivesTest, ObsDistinct) {
    run("array-new 1 array-push 1 array-push 2 array-push 2 array-push 3 array-push obs-from obs-distinct obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 1);
    arr->get(1, v); EXPECT_EQ(v.as_int, 2);
    arr->get(2, v); EXPECT_EQ(v.as_int, 3);
    arr->release();
}

// --- Combination: obs-concat ---

TEST_F(ObservablePrimitivesTest, ObsConcat) {
    run("1 3 obs-range 10 13 obs-range obs-concat obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 5u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 1);
    arr->get(1, v); EXPECT_EQ(v.as_int, 2);
    arr->get(2, v); EXPECT_EQ(v.as_int, 10);
    arr->get(3, v); EXPECT_EQ(v.as_int, 11);
    arr->get(4, v); EXPECT_EQ(v.as_int, 12);
    arr->release();
}

// --- Combination: obs-merge (synchronous) ---

TEST_F(ObservablePrimitivesTest, ObsMerge) {
    run("1 3 obs-range 10 13 obs-range 0 obs-merge obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 5u);
    arr->release();
}

// --- Combination: obs-zip ---

TEST_F(ObservablePrimitivesTest, ObsZip) {
    run("1 4 obs-range 10 13 obs-range obs-zip obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 3u);
    // Each element is a 2-element array [a, b]
    Value pair;
    arr->get(0, pair);
    EXPECT_EQ(pair.type, Value::Type::Array);
    auto* p = pair.as_array();
    Value v;
    p->get(0, v); EXPECT_EQ(v.as_int, 1);
    p->get(1, v); EXPECT_EQ(v.as_int, 10);
    pair.release();
    arr->release();
}

// --- Terminal: obs-subscribe ---

TEST_F(ObservablePrimitivesTest, ObsSubscribe) {
    run(": print-it . ;");
    run("1 4 obs-range ' print-it obs-subscribe");
    EXPECT_EQ(out.str(), "1 2 3 ");
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

// --- Introspection ---

TEST_F(ObservablePrimitivesTest, ObsCheck) {
    run("1 5 obs-range obs?");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_TRUE(opt->as_bool());
}

TEST_F(ObservablePrimitivesTest, ObsCheckNonObs) {
    run("42 obs?");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_FALSE(opt->as_bool());
}

TEST_F(ObservablePrimitivesTest, ObsKind) {
    run("1 5 obs-range obs-kind");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    EXPECT_EQ(opt->as_string()->view(), "range");
    opt->release();
}

// --- Pipeline composition ---

TEST_F(ObservablePrimitivesTest, MapFilterReduce) {
    run(": double dup + ;");
    run(": gt5? 5 > ;");
    run(": add + ;");
    run("1 6 obs-range ' double obs-map ' gt5? obs-filter ' add 0 obs-reduce");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    // range: 1..5, doubled: 2,4,6,8,10, >5: 6,8,10, sum: 24
    EXPECT_EQ(opt->as_int, 24);
}

TEST_F(ObservablePrimitivesTest, TakeFromInfiniteRange) {
    // obs-range with a large end, take 3 — should stop early
    run("0 1000000 obs-range 3 obs-take obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 0);
    arr->get(1, v); EXPECT_EQ(v.as_int, 1);
    arr->get(2, v); EXPECT_EQ(v.as_int, 2);
    arr->release();
}

// --- obs-map-with ---

TEST_F(ObservablePrimitivesTest, ObsMapWith) {
    run(": add-ctx + ;");
    run("1 4 obs-range ' add-ctx 100 obs-map-with obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 101);
    arr->get(1, v); EXPECT_EQ(v.as_int, 102);
    arr->get(2, v); EXPECT_EQ(v.as_int, 103);
    arr->release();
}

// --- obs-filter-with ---

TEST_F(ObservablePrimitivesTest, ObsFilterWith) {
    // filter-with pushes ( ctx val ) so xt sees ctx under val
    // xt: < compares ctx < val, passing vals > ctx (> 5)
    run(": lt? < ;");
    run("1 10 obs-range ' lt? 5 obs-filter-with obs-to-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    ASSERT_EQ(arr->length(), 4u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 6);
    arr->get(1, v); EXPECT_EQ(v.as_int, 7);
    arr->get(2, v); EXPECT_EQ(v.as_int, 8);
    arr->get(3, v); EXPECT_EQ(v.as_int, 9);
    arr->release();
}

// --- Type display ---

TEST_F(ObservablePrimitivesTest, DotPrintsObservable) {
    run("1 5 obs-range .");
    EXPECT_NE(out.str().find("observable"), std::string::npos);
}

TEST_F(ObservablePrimitivesTest, DotSShowsObservable) {
    run("1 5 obs-range .s");
    EXPECT_NE(out.str().find("observable"), std::string::npos);
    // .s is non-destructive, clean up
    auto opt = ctx().data_stack().pop();
    if (opt) opt->release();
}
