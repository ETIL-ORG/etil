// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/heap_byte_array.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/heap_primitives.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>

using namespace etil::core;

// --- HeapObject taint field ---

TEST(TaintTest, DefaultNotTainted) {
    auto* hs = HeapString::create("hello");
    EXPECT_FALSE(hs->is_tainted());
    hs->release();
}

TEST(TaintTest, SetTainted) {
    auto* hs = HeapString::create("hello");
    hs->set_tainted(true);
    EXPECT_TRUE(hs->is_tainted());
    hs->release();
}

TEST(TaintTest, CreateTaintedFactory) {
    auto* hs = HeapString::create_tainted("untrusted");
    EXPECT_TRUE(hs->is_tainted());
    EXPECT_EQ(hs->view(), "untrusted");
    hs->release();
}

TEST(TaintTest, ByteArrayDefaultNotTainted) {
    auto* ba = new HeapByteArray(10);
    EXPECT_FALSE(ba->is_tainted());
    ba->release();
}

TEST(TaintTest, ByteArraySetTainted) {
    auto* ba = new HeapByteArray(10);
    ba->set_tainted(true);
    EXPECT_TRUE(ba->is_tainted());
    ba->release();
}

TEST(TaintTest, SizeOfHeapObjectUnchanged) {
    // HeapObject layout: vtable(8) + kind(4) + tainted(1) + pad(3) + atomic(8) = 24
    // Tainted fits in the existing alignment padding.
    EXPECT_LE(sizeof(HeapObject), 24u);
}

// --- staint primitive ---

class TaintPrimTest : public ::testing::Test {
protected:
    void SetUp() override {
        register_primitives(dict_);
        register_string_primitives(dict_);
        register_byte_primitives(dict_);
        ctx_.set_dictionary(&dict_);
    }

    Dictionary dict_;
    ExecutionContext ctx_{0};
};

TEST_F(TaintPrimTest, StaintCleanString) {
    auto* hs = HeapString::create("clean");
    ctx_.data_stack().push(Value::from(hs));

    auto impl = dict_.lookup("staint");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Boolean);
    EXPECT_FALSE(result->as_bool());
}

TEST_F(TaintPrimTest, StaintTaintedString) {
    auto* hs = HeapString::create_tainted("dirty");
    ctx_.data_stack().push(Value::from(hs));

    auto impl = dict_.lookup("staint");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Boolean);
    EXPECT_TRUE(result->as_bool());
}

// --- s+ propagation ---

TEST_F(TaintPrimTest, SplusBothClean) {
    ctx_.data_stack().push(Value::from(HeapString::create("a")));
    ctx_.data_stack().push(Value::from(HeapString::create("b")));

    auto impl = dict_.lookup("s+");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::String);
    auto* hs = result->as_string();
    EXPECT_FALSE(hs->is_tainted());
    EXPECT_EQ(hs->view(), "ab");
    hs->release();
}

TEST_F(TaintPrimTest, SplusTaintedFirst) {
    ctx_.data_stack().push(Value::from(HeapString::create_tainted("a")));
    ctx_.data_stack().push(Value::from(HeapString::create("b")));

    auto impl = dict_.lookup("s+");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* hs = result->as_string();
    EXPECT_TRUE(hs->is_tainted());
    hs->release();
}

TEST_F(TaintPrimTest, SplusTaintedSecond) {
    ctx_.data_stack().push(Value::from(HeapString::create("a")));
    ctx_.data_stack().push(Value::from(HeapString::create_tainted("b")));

    auto impl = dict_.lookup("s+");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* hs = result->as_string();
    EXPECT_TRUE(hs->is_tainted());
    hs->release();
}

// --- substr propagation ---

TEST_F(TaintPrimTest, SubstrPropagatesTaint) {
    ctx_.data_stack().push(Value::from(HeapString::create_tainted("hello world")));
    ctx_.data_stack().push(Value(int64_t(0)));
    ctx_.data_stack().push(Value(int64_t(5)));

    auto impl = dict_.lookup("substr");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* hs = result->as_string();
    EXPECT_TRUE(hs->is_tainted());
    EXPECT_EQ(hs->view(), "hello");
    hs->release();
}

// --- strim propagation ---

TEST_F(TaintPrimTest, StrimPropagatesTaint) {
    ctx_.data_stack().push(Value::from(HeapString::create_tainted("  hello  ")));

    auto impl = dict_.lookup("strim");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* hs = result->as_string();
    EXPECT_TRUE(hs->is_tainted());
    EXPECT_EQ(hs->view(), "hello");
    hs->release();
}

// --- sreplace propagation ---

TEST_F(TaintPrimTest, SreplacePropagatesTaint) {
    ctx_.data_stack().push(Value::from(HeapString::create_tainted("hello world")));
    ctx_.data_stack().push(Value::from(HeapString::create("world")));
    ctx_.data_stack().push(Value::from(HeapString::create("ETIL")));

    auto impl = dict_.lookup("sreplace");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* hs = result->as_string();
    EXPECT_TRUE(hs->is_tainted());
    EXPECT_EQ(hs->view(), "hello ETIL");
    hs->release();
}

// --- sregex-replace untaints ---

TEST_F(TaintPrimTest, SregexReplaceUntaints) {
    ctx_.data_stack().push(Value::from(HeapString::create_tainted("abc123")));
    ctx_.data_stack().push(Value::from(HeapString::create("[0-9]")));
    ctx_.data_stack().push(Value::from(HeapString::create("X")));

    auto impl = dict_.lookup("sregex-replace");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* hs = result->as_string();
    EXPECT_FALSE(hs->is_tainted());  // regex sanitization untaints
    EXPECT_EQ(hs->view(), "abcXXX");
    hs->release();
}

// --- bytes<->string propagation ---

TEST_F(TaintPrimTest, BytesToStringPropagatesTaint) {
    auto* ba = new HeapByteArray(5);
    std::memcpy(ba->data(), "hello", 5);
    ba->set_tainted(true);
    ctx_.data_stack().push(Value::from(ba));

    auto impl = dict_.lookup("bytes->string");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* hs = result->as_string();
    EXPECT_TRUE(hs->is_tainted());
    EXPECT_EQ(hs->view(), "hello");
    hs->release();
}

TEST_F(TaintPrimTest, StringToBytesPropagatesTaint) {
    ctx_.data_stack().push(Value::from(HeapString::create_tainted("hello")));

    auto impl = dict_.lookup("string->bytes");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* ba = result->as_byte_array();
    EXPECT_TRUE(ba->is_tainted());
    ba->release();
}

// --- ssplit propagation ---

TEST_F(TaintPrimTest, SsplitPropagatesTaint) {
    ctx_.data_stack().push(Value::from(HeapString::create_tainted("a,b,c")));
    ctx_.data_stack().push(Value::from(HeapString::create(",")));

    auto impl = dict_.lookup("ssplit");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* arr = result->as_array();
    EXPECT_EQ(arr->length(), 3u);
    // Each element should be tainted
    Value elem;
    ASSERT_TRUE(arr->get(0, elem));
    EXPECT_TRUE(elem.as_string()->is_tainted());
    elem.as_string()->release();
    ASSERT_TRUE(arr->get(1, elem));
    EXPECT_TRUE(elem.as_string()->is_tainted());
    elem.as_string()->release();
    arr->release();
}

// --- sjoin propagation ---

TEST_F(TaintPrimTest, SjoinPropagatesTaint) {
    auto* arr = new HeapArray();
    arr->push_back(Value::from(HeapString::create_tainted("a")));
    arr->push_back(Value::from(HeapString::create("b")));
    ctx_.data_stack().push(Value::from(arr));
    ctx_.data_stack().push(Value::from(HeapString::create(",")));

    auto impl = dict_.lookup("sjoin");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE((*impl)->native_code()(ctx_));

    auto result = ctx_.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* hs = result->as_string();
    EXPECT_TRUE(hs->is_tainted());
    EXPECT_EQ(hs->view(), "a,b");
    hs->release();
}

// --- staint via interpreter ---

class TaintInterpreterTest : public ::testing::Test {
protected:
    void SetUp() override {
        register_primitives(dict_);
        register_string_primitives(dict_);
        interp_ = std::make_unique<Interpreter>(dict_, out_, err_);
    }
    void TearDown() override {
        interp_->shutdown();
    }

    Dictionary dict_;
    std::ostringstream out_, err_;
    std::unique_ptr<Interpreter> interp_;
};

TEST_F(TaintInterpreterTest, LiteralStringNotTainted) {
    interp_->interpret_line(R"(s" hello" staint .)");
    EXPECT_EQ(out_.str(), "false ");
}
