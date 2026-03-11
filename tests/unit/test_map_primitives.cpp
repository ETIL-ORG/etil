// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::core;

class MapPrimitivesTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    Interpreter interp{dict, out};

    void SetUp() override {
        register_primitives(dict);
    }

    void run(const std::string& code) {
        out.str("");
        interp.interpret_line(code);
    }

    ExecutionContext& ctx() { return interp.context(); }
};

TEST_F(MapPrimitivesTest, MapNew) {
    run("map-new");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Map);
    auto* m = opt->as_map();
    EXPECT_EQ(m->size(), 0u);
    m->release();
}

TEST_F(MapPrimitivesTest, MapSetAndGet) {
    run("map-new s\" name\" s\" Alice\" map-set dup s\" name\" map-get");
    // TOS should be the string "Alice"
    auto opt_val = ctx().data_stack().pop();
    ASSERT_TRUE(opt_val.has_value());
    EXPECT_EQ(opt_val->type, Value::Type::String);
    auto* hs = opt_val->as_string();
    EXPECT_EQ(hs->view(), "Alice");
    hs->release();
    // Map should still be on stack
    auto opt_map = ctx().data_stack().pop();
    ASSERT_TRUE(opt_map.has_value());
    EXPECT_EQ(opt_map->type, Value::Type::Map);
    opt_map->release();
}

TEST_F(MapPrimitivesTest, MapSetOverwrite) {
    run("map-new s\" x\" 10 map-set s\" x\" 20 map-set dup s\" x\" map-get");
    auto opt_val = ctx().data_stack().pop();
    ASSERT_TRUE(opt_val.has_value());
    EXPECT_EQ(opt_val->type, Value::Type::Integer);
    EXPECT_EQ(opt_val->as_int, 20);
    // Clean up map
    auto opt_map = ctx().data_stack().pop();
    ASSERT_TRUE(opt_map.has_value());
    opt_map->release();
}

TEST_F(MapPrimitivesTest, MapGetMissing) {
    run("map-new");
    // Push the map and a key that doesn't exist
    auto* hs = HeapString::create("nokey");
    ctx().data_stack().push(Value::from(hs));
    // map-get should return false (stack restored)
    EXPECT_FALSE(prim_map_get(ctx()));
    // Stack should have map and key restored
    auto opt_key = ctx().data_stack().pop();
    ASSERT_TRUE(opt_key.has_value());
    EXPECT_EQ(opt_key->type, Value::Type::String);
    opt_key->release();
    auto opt_map = ctx().data_stack().pop();
    ASSERT_TRUE(opt_map.has_value());
    EXPECT_EQ(opt_map->type, Value::Type::Map);
    opt_map->release();
}

TEST_F(MapPrimitivesTest, MapRemove) {
    run("map-new s\" a\" 1 map-set s\" b\" 2 map-set s\" a\" map-remove");
    // TOS is the map
    auto opt_map = ctx().data_stack().pop();
    ASSERT_TRUE(opt_map.has_value());
    EXPECT_EQ(opt_map->type, Value::Type::Map);
    auto* m = opt_map->as_map();
    EXPECT_EQ(m->size(), 1u);
    EXPECT_FALSE(m->has("a"));
    EXPECT_TRUE(m->has("b"));
    m->release();
}

TEST_F(MapPrimitivesTest, MapRemoveMissing) {
    run("map-new");
    auto* hs = HeapString::create("nokey");
    ctx().data_stack().push(Value::from(hs));
    EXPECT_FALSE(prim_map_remove(ctx()));
    // Stack restored
    auto opt_key = ctx().data_stack().pop();
    ASSERT_TRUE(opt_key.has_value());
    opt_key->release();
    auto opt_map = ctx().data_stack().pop();
    ASSERT_TRUE(opt_map.has_value());
    opt_map->release();
}

TEST_F(MapPrimitivesTest, MapLength) {
    run("map-new s\" a\" 1 map-set s\" b\" 2 map-set s\" c\" 3 map-set map-length");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Integer);
    EXPECT_EQ(opt->as_int, 3);
}

TEST_F(MapPrimitivesTest, MapKeys) {
    run("map-new s\" x\" 10 map-set s\" y\" 20 map-set map-keys");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Array);
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 2u);
    // Verify both keys are present (order may vary)
    std::vector<std::string> keys;
    for (size_t i = 0; i < arr->length(); ++i) {
        Value v;
        ASSERT_TRUE(arr->get(i, v));
        EXPECT_EQ(v.type, Value::Type::String);
        auto* s = v.as_string();
        keys.push_back(std::string(s->view()));
        s->release();
    }
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys[0], "x");
    EXPECT_EQ(keys[1], "y");
    arr->release();
}

TEST_F(MapPrimitivesTest, MapValues) {
    run("map-new s\" a\" 100 map-set s\" b\" 200 map-set map-values");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Array);
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 2u);
    // Verify both values are present (order may vary)
    std::vector<int64_t> vals;
    for (size_t i = 0; i < arr->length(); ++i) {
        Value v;
        ASSERT_TRUE(arr->get(i, v));
        EXPECT_EQ(v.type, Value::Type::Integer);
        vals.push_back(v.as_int);
    }
    std::sort(vals.begin(), vals.end());
    EXPECT_EQ(vals[0], 100);
    EXPECT_EQ(vals[1], 200);
    arr->release();
}

TEST_F(MapPrimitivesTest, MapHasTrue) {
    run("map-new s\" key\" 42 map-set s\" key\" map-has?");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_TRUE(opt->as_bool());
}

TEST_F(MapPrimitivesTest, MapHasFalse) {
    run("map-new s\" nope\" map-has?");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_FALSE(opt->as_bool());
}

TEST_F(MapPrimitivesTest, MapUnderflow) {
    // map-get on empty stack should fail
    EXPECT_FALSE(prim_map_get(ctx()));
    EXPECT_FALSE(prim_map_set(ctx()));
    EXPECT_FALSE(prim_map_remove(ctx()));
    EXPECT_FALSE(prim_map_length(ctx()));
    EXPECT_FALSE(prim_map_keys(ctx()));
    EXPECT_FALSE(prim_map_values(ctx()));
    EXPECT_FALSE(prim_map_has(ctx()));
}

// --- Type mismatch tests: non-map where map expected ---

TEST_F(MapPrimitivesTest, MapSetNonMapRestoresStack) {
    // Stack: ( string key val ) — string where map expected
    auto* s = HeapString::create("not-a-map");
    ctx().data_stack().push(Value::from(s));
    ctx().data_stack().push(Value::from(HeapString::create("key")));
    ctx().data_stack().push(Value(int64_t(42)));

    size_t depth_before = ctx().data_stack().size();
    EXPECT_FALSE(prim_map_set(ctx()));
    // Stack fully restored: ( string key val )
    EXPECT_EQ(ctx().data_stack().size(), depth_before);

    auto opt_val = ctx().data_stack().pop();
    EXPECT_EQ(opt_val->as_int, 42);
    auto opt_key = ctx().data_stack().pop();
    EXPECT_EQ(opt_key->type, Value::Type::String);
    EXPECT_EQ(opt_key->as_string()->view(), "key");
    opt_key->release();
    auto opt_str = ctx().data_stack().pop();
    EXPECT_EQ(opt_str->type, Value::Type::String);
    EXPECT_EQ(opt_str->as_string()->view(), "not-a-map");
    opt_str->release();
}

TEST_F(MapPrimitivesTest, MapGetNonMapRestoresStack) {
    // Stack: ( integer key ) — integer where map expected
    ctx().data_stack().push(Value(int64_t(99)));
    ctx().data_stack().push(Value::from(HeapString::create("key")));

    size_t depth_before = ctx().data_stack().size();
    EXPECT_FALSE(prim_map_get(ctx()));
    EXPECT_EQ(ctx().data_stack().size(), depth_before);

    auto opt_key = ctx().data_stack().pop();
    EXPECT_EQ(opt_key->type, Value::Type::String);
    EXPECT_EQ(opt_key->as_string()->view(), "key");
    opt_key->release();
    auto opt_int = ctx().data_stack().pop();
    EXPECT_EQ(opt_int->as_int, 99);
}

TEST_F(MapPrimitivesTest, MapRemoveNonMapRestoresStack) {
    ctx().data_stack().push(Value(int64_t(77)));
    ctx().data_stack().push(Value::from(HeapString::create("key")));

    size_t depth_before = ctx().data_stack().size();
    EXPECT_FALSE(prim_map_remove(ctx()));
    EXPECT_EQ(ctx().data_stack().size(), depth_before);

    auto opt_key = ctx().data_stack().pop();
    EXPECT_EQ(opt_key->type, Value::Type::String);
    opt_key->release();
    auto opt_int = ctx().data_stack().pop();
    EXPECT_EQ(opt_int->as_int, 77);
}

TEST_F(MapPrimitivesTest, MapHasNonMapRestoresStack) {
    ctx().data_stack().push(Value(int64_t(55)));
    ctx().data_stack().push(Value::from(HeapString::create("key")));

    size_t depth_before = ctx().data_stack().size();
    EXPECT_FALSE(prim_map_has(ctx()));
    EXPECT_EQ(ctx().data_stack().size(), depth_before);

    auto opt_key = ctx().data_stack().pop();
    EXPECT_EQ(opt_key->type, Value::Type::String);
    opt_key->release();
    auto opt_int = ctx().data_stack().pop();
    EXPECT_EQ(opt_int->as_int, 55);
}

TEST_F(MapPrimitivesTest, MapLengthNonMapRestoresStack) {
    // pop_map now pushes back on type mismatch
    ctx().data_stack().push(Value(int64_t(33)));

    size_t depth_before = ctx().data_stack().size();
    EXPECT_FALSE(prim_map_length(ctx()));
    EXPECT_EQ(ctx().data_stack().size(), depth_before);

    auto opt = ctx().data_stack().pop();
    EXPECT_EQ(opt->as_int, 33);
}

TEST_F(MapPrimitivesTest, MapKeysNonMapRestoresStack) {
    ctx().data_stack().push(Value::from(HeapString::create("not-a-map")));

    size_t depth_before = ctx().data_stack().size();
    EXPECT_FALSE(prim_map_keys(ctx()));
    EXPECT_EQ(ctx().data_stack().size(), depth_before);

    auto opt = ctx().data_stack().pop();
    EXPECT_EQ(opt->type, Value::Type::String);
    opt->release();
}

TEST_F(MapPrimitivesTest, MapValuesNonMapRestoresStack) {
    ctx().data_stack().push(Value(true));

    size_t depth_before = ctx().data_stack().size();
    EXPECT_FALSE(prim_map_values(ctx()));
    EXPECT_EQ(ctx().data_stack().size(), depth_before);

    auto opt = ctx().data_stack().pop();
    EXPECT_EQ(opt->type, Value::Type::Boolean);
}
