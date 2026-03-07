// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_byte_array.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::core;

class BytePrimitivesTest : public ::testing::Test {
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

TEST_F(BytePrimitivesTest, BytesNew) {
    run("16 bytes-new");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::ByteArray);
    auto* ba = opt->as_byte_array();
    EXPECT_EQ(ba->length(), 16u);
    ba->release();
}

TEST_F(BytePrimitivesTest, BytesGetSet) {
    run("4 bytes-new 0 255 bytes-set 0 bytes-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 255);
}

TEST_F(BytePrimitivesTest, BytesLength) {
    run("10 bytes-new bytes-length");
    auto opt_len = ctx().data_stack().pop();
    ASSERT_TRUE(opt_len.has_value());
    EXPECT_EQ(opt_len->as_int, 10);
    auto opt_ba = ctx().data_stack().pop();
    opt_ba->release();
}

TEST_F(BytePrimitivesTest, BytesResize) {
    run("4 bytes-new 0 42 bytes-set 8 bytes-resize bytes-length");
    auto opt_len = ctx().data_stack().pop();
    ASSERT_TRUE(opt_len.has_value());
    EXPECT_EQ(opt_len->as_int, 8);
    auto opt_ba = ctx().data_stack().pop();
    auto* ba = opt_ba->as_byte_array();
    uint8_t v;
    ba->get(0, v);
    EXPECT_EQ(v, 42);  // original data preserved
    ba->get(4, v);
    EXPECT_EQ(v, 0);   // new bytes zeroed
    ba->release();
}

TEST_F(BytePrimitivesTest, BytesGetOutOfBounds) {
    run("2 bytes-new 5 bytes-get");
    // Should fail - stack should have ba and idx back
    EXPECT_GE(ctx().data_stack().size(), 2u);
    while (auto v = ctx().data_stack().pop()) {
        v->release();
    }
}

TEST_F(BytePrimitivesTest, BytesToString) {
    // Create bytes with "hi" content
    auto* ba = new HeapByteArray(2);
    ba->set(0, 'h');
    ba->set(1, 'i');
    ctx().data_stack().push(Value::from(ba));
    run("bytes->string");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    auto* hs = opt->as_string();
    EXPECT_EQ(hs->view(), "hi");
    hs->release();
}

TEST_F(BytePrimitivesTest, StringToBytes) {
    auto* hs = HeapString::create("abc");
    ctx().data_stack().push(Value::from(hs));
    run("string->bytes");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::ByteArray);
    auto* ba = opt->as_byte_array();
    EXPECT_EQ(ba->length(), 3u);
    uint8_t v;
    ba->get(0, v); EXPECT_EQ(v, 'a');
    ba->get(1, v); EXPECT_EQ(v, 'b');
    ba->get(2, v); EXPECT_EQ(v, 'c');
    ba->release();
}

TEST_F(BytePrimitivesTest, RoundTripConversion) {
    auto* hs = HeapString::create("hello");
    ctx().data_stack().push(Value::from(hs));
    run("string->bytes bytes->string");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    auto* result = opt->as_string();
    EXPECT_EQ(result->view(), "hello");
    result->release();
}
