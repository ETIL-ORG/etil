// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::core;

class ArrayPrimitivesTest : public ::testing::Test {
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

TEST_F(ArrayPrimitivesTest, ArrayNew) {
    run("array-new");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Array);
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 0u);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayPushAndLength) {
    run("array-new 10 array-push 20 array-push array-length");
    auto opt_len = ctx().data_stack().pop();
    ASSERT_TRUE(opt_len.has_value());
    EXPECT_EQ(opt_len->as_int, 2);
    auto opt_arr = ctx().data_stack().pop();
    ASSERT_TRUE(opt_arr.has_value());
    opt_arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayPushPop) {
    run("array-new 42 array-push 99 array-push array-pop");
    // Stack: arr 99
    auto opt_val = ctx().data_stack().pop();
    ASSERT_TRUE(opt_val.has_value());
    EXPECT_EQ(opt_val->as_int, 99);
    auto opt_arr = ctx().data_stack().pop();
    ASSERT_TRUE(opt_arr.has_value());
    auto* arr = opt_arr->as_array();
    EXPECT_EQ(arr->length(), 1u);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayGet) {
    run("array-new 10 array-push 20 array-push 30 array-push 1 array-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 20);
}

TEST_F(ArrayPrimitivesTest, ArraySet) {
    run("array-new 10 array-push 20 array-push 1 99 array-set");
    auto opt_arr = ctx().data_stack().pop();
    ASSERT_TRUE(opt_arr.has_value());
    auto* arr = opt_arr->as_array();
    Value v;
    arr->get(1, v);
    EXPECT_EQ(v.as_int, 99);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayShiftUnshift) {
    run("array-new 10 array-push 20 array-push 5 array-unshift");
    // arr = [5, 10, 20]
    run("array-shift");
    // Stack: arr 5
    auto opt_val = ctx().data_stack().pop();
    ASSERT_TRUE(opt_val.has_value());
    EXPECT_EQ(opt_val->as_int, 5);
    auto opt_arr = ctx().data_stack().pop();
    auto* arr = opt_arr->as_array();
    EXPECT_EQ(arr->length(), 2u);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayCompact) {
    run("array-new 1 array-push 2 array-push 3 array-push array-compact");
    auto opt_arr = ctx().data_stack().pop();
    ASSERT_TRUE(opt_arr.has_value());
    auto* arr = opt_arr->as_array();
    EXPECT_EQ(arr->length(), 3u);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayGetOutOfBounds) {
    run("array-new 10 array-push 5 array-get");
    // Should fail — check stack has arr and idx back
    EXPECT_GE(ctx().data_stack().size(), 2u);
    // Clean up
    while (auto v = ctx().data_stack().pop()) {
        v->release();
    }
}

TEST_F(ArrayPrimitivesTest, NestedArrays) {
    // Create inner array, push onto outer
    run("array-new");
    auto* inner = new HeapArray();
    inner->push_back(Value(int64_t(42)));
    ctx().data_stack().push(Value::from(inner));
    run("array-push");
    // Outer array now contains inner array
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* outer = opt->as_array();
    EXPECT_EQ(outer->length(), 1u);
    Value v;
    outer->get(0, v);
    EXPECT_EQ(v.type, Value::Type::Array);
    v.release();
    outer->release();
}

TEST_F(ArrayPrimitivesTest, ArrayReverse) {
    run("array-new 1 array-push 2 array-push 3 array-push array-reverse");
    auto opt_arr = ctx().data_stack().pop();
    ASSERT_TRUE(opt_arr.has_value());
    auto* arr = opt_arr->as_array();
    EXPECT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 3);
    arr->get(1, v); EXPECT_EQ(v.as_int, 2);
    arr->get(2, v); EXPECT_EQ(v.as_int, 1);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayReverseEmpty) {
    run("array-new array-reverse");
    auto opt_arr = ctx().data_stack().pop();
    ASSERT_TRUE(opt_arr.has_value());
    auto* arr = opt_arr->as_array();
    EXPECT_EQ(arr->length(), 0u);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayReverseSingle) {
    run("array-new 42 array-push array-reverse");
    auto opt_arr = ctx().data_stack().pop();
    ASSERT_TRUE(opt_arr.has_value());
    auto* arr = opt_arr->as_array();
    EXPECT_EQ(arr->length(), 1u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 42);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, MixedTypes) {
    auto* arr = new HeapArray();
    arr->push_back(Value(int64_t(42)));
    arr->push_back(Value(3.14));
    arr->push_back(Value::from(HeapString::create("hello")));
    EXPECT_EQ(arr->length(), 3u);

    Value v;
    arr->get(0, v); EXPECT_EQ(v.type, Value::Type::Integer);
    arr->get(1, v); EXPECT_EQ(v.type, Value::Type::Float);
    arr->get(2, v);
    EXPECT_EQ(v.type, Value::Type::String);
    v.release();

    arr->release();
}

// ---------------------------------------------------------------------------
// Array iteration primitives
// ---------------------------------------------------------------------------

TEST_F(ArrayPrimitivesTest, ArrayEach) {
    // Define a word that prints each element
    run(": double dup + ;");
    // array-each should execute xt for each element, consuming each
    run("array-new 10 array-push 20 array-push 30 array-push");
    run("' . array-each");
    EXPECT_EQ(out.str(), "10 20 30 ");
}

TEST_F(ArrayPrimitivesTest, ArrayEachEmpty) {
    run("array-new ' . array-each");
    EXPECT_EQ(out.str(), "");
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

TEST_F(ArrayPrimitivesTest, ArrayMap) {
    run(": double dup + ;");
    run("array-new 1 array-push 2 array-push 3 array-push");
    run("' double array-map");
    auto& stack = ctx().data_stack();
    EXPECT_EQ(stack.size(), 1u);
    auto opt = stack.pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Array);
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 3u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 2);
    arr->get(1, v); EXPECT_EQ(v.as_int, 4);
    arr->get(2, v); EXPECT_EQ(v.as_int, 6);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayMapEmpty) {
    run(": double dup + ;");
    run("array-new ' double array-map");
    auto& stack = ctx().data_stack();
    EXPECT_EQ(stack.size(), 1u);
    auto opt = stack.pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 0u);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayFilter) {
    run(": big? 10 > ;");
    run("array-new 5 array-push 15 array-push 3 array-push 20 array-push");
    run("' big? array-filter");
    auto& stack = ctx().data_stack();
    EXPECT_EQ(stack.size(), 1u);
    auto opt = stack.pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 2u);
    Value v;
    arr->get(0, v); EXPECT_EQ(v.as_int, 15);
    arr->get(1, v); EXPECT_EQ(v.as_int, 20);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayFilterNonePass) {
    run(": big? 100 > ;");
    run("array-new 1 array-push 2 array-push");
    run("' big? array-filter");
    auto& stack = ctx().data_stack();
    auto opt = stack.pop();
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 0u);
    arr->release();
}

TEST_F(ArrayPrimitivesTest, ArrayReduce) {
    run("array-new 1 array-push 2 array-push 3 array-push 4 array-push");
    run("' + 0 array-reduce");
    auto& stack = ctx().data_stack();
    EXPECT_EQ(stack.size(), 1u);
    auto opt = stack.pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 10);
}

TEST_F(ArrayPrimitivesTest, ArrayReduceEmpty) {
    run("array-new ' + 0 array-reduce");
    auto& stack = ctx().data_stack();
    EXPECT_EQ(stack.size(), 1u);
    auto opt = stack.pop();
    EXPECT_EQ(opt->as_int, 0);  // returns init value
}

TEST_F(ArrayPrimitivesTest, ArrayReduceStrings) {
    run("array-new s\" hello\" array-push s\" world\" array-push");
    run("' s+ s\" \" array-reduce");  // init is empty string (s" " → "")
    auto& stack = ctx().data_stack();
    auto opt = stack.pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    EXPECT_EQ(opt->as_string()->view(), "helloworld");
    opt->release();
}

TEST_F(ArrayPrimitivesTest, ArrayMapWithStrings) {
    // Map over strings — upcase-like operation using s+ to append "!"
    run(": bang s\" !\" s+ ;");
    run("array-new s\" hi\" array-push s\" bye\" array-push");
    run("' bang array-map");
    auto& stack = ctx().data_stack();
    auto opt = stack.pop();
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 2u);
    Value v;
    arr->get(0, v);
    EXPECT_EQ(v.as_string()->view(), "hi!");
    v.release();
    arr->get(1, v);
    EXPECT_EQ(v.as_string()->view(), "bye!");
    v.release();
    arr->release();
}
