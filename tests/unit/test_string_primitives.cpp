// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::core;

class StringPrimitivesTest : public ::testing::Test {
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

    // Push a HeapString onto the stack
    void push_string(const std::string& s) {
        auto* hs = HeapString::create(s);
        ctx().data_stack().push(Value::from(hs));
    }

    // Pop and return string content, releasing the ref
    std::string pop_string() {
        auto opt = ctx().data_stack().pop();
        if (!opt || opt->type != Value::Type::String || !opt->as_ptr) return "";
        auto* hs = opt->as_string();
        std::string result(hs->view());
        hs->release();
        return result;
    }
};

// --- s" parsing ---

TEST_F(StringPrimitivesTest, SQuoteInterpretMode) {
    run("s\" hello\"");
    EXPECT_EQ(ctx().data_stack().size(), 1u);
    EXPECT_EQ(pop_string(), "hello");
}

TEST_F(StringPrimitivesTest, SQuoteCompileMode) {
    run(": test s\" world\" ;");
    run("test");
    EXPECT_EQ(ctx().data_stack().size(), 1u);
    EXPECT_EQ(pop_string(), "world");
}

// --- type / s. ---

TEST_F(StringPrimitivesTest, Type) {
    run("s\" hello\" type");
    EXPECT_EQ(out.str(), "hello");
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

TEST_F(StringPrimitivesTest, SDot) {
    run("s\" test\" s.");
    EXPECT_EQ(out.str(), "test");
}

// --- s+ ---

TEST_F(StringPrimitivesTest, SPlus) {
    run("s\" hello\" s\" world\" s+");
    EXPECT_EQ(pop_string(), "helloworld");
}

TEST_F(StringPrimitivesTest, SPlusEmpty) {
    run("s\" hello\" s\" \" s+");
    EXPECT_EQ(pop_string(), "hello");
}

// --- s+ auto-conversion ---

TEST_F(StringPrimitivesTest, SplusIntString) {
    ctx().data_stack().push(Value(int64_t(42)));
    push_string("hello");
    run("s+");
    EXPECT_EQ(pop_string(), "42hello");
}

TEST_F(StringPrimitivesTest, SplusStringInt) {
    push_string("value:");
    ctx().data_stack().push(Value(int64_t(42)));
    run("s+");
    EXPECT_EQ(pop_string(), "value:42");
}

TEST_F(StringPrimitivesTest, SplusIntInt) {
    ctx().data_stack().push(Value(int64_t(42)));
    ctx().data_stack().push(Value(int64_t(7)));
    run("s+");
    EXPECT_EQ(pop_string(), "427");
}

TEST_F(StringPrimitivesTest, SplusFloatString) {
    ctx().data_stack().push(Value(3.14));
    push_string("pi");
    run("s+");
    EXPECT_EQ(pop_string(), "3.14pi");
}

TEST_F(StringPrimitivesTest, SplusStringFloat) {
    push_string("pi=");
    ctx().data_stack().push(Value(3.14));
    run("s+");
    EXPECT_EQ(pop_string(), "pi=3.14");
}

// --- s= / s<> ---

TEST_F(StringPrimitivesTest, SEqualTrue) {
    run("s\" abc\" s\" abc\" s=");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_TRUE(opt->as_bool());
}

TEST_F(StringPrimitivesTest, SEqualFalse) {
    run("s\" abc\" s\" def\" s=");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_FALSE(opt->as_bool());
}

TEST_F(StringPrimitivesTest, SNotEqual) {
    run("s\" abc\" s\" def\" s<>");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_TRUE(opt->as_bool());
}

// --- slength ---

TEST_F(StringPrimitivesTest, SLength) {
    run("s\" hello\" slength");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 5);
}

TEST_F(StringPrimitivesTest, SLengthEmpty) {
    run("s\" \" slength");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 0);
}

// --- substr ---

TEST_F(StringPrimitivesTest, Substr) {
    push_string("hello world");
    ctx().data_stack().push(Value(int64_t(6)));
    ctx().data_stack().push(Value(int64_t(5)));
    run("substr");
    EXPECT_EQ(pop_string(), "world");
}

TEST_F(StringPrimitivesTest, SubstrClamped) {
    push_string("hi");
    ctx().data_stack().push(Value(int64_t(0)));
    ctx().data_stack().push(Value(int64_t(100)));
    run("substr");
    EXPECT_EQ(pop_string(), "hi");
}

// --- strim ---

TEST_F(StringPrimitivesTest, STrim) {
    push_string("  hello  ");
    run("strim");
    EXPECT_EQ(pop_string(), "hello");
}

TEST_F(StringPrimitivesTest, STrimAllWhitespace) {
    push_string("   ");
    run("strim");
    EXPECT_EQ(pop_string(), "");
}

// --- sfind ---

TEST_F(StringPrimitivesTest, SFind) {
    push_string("hello world");
    push_string("world");
    run("sfind");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 6);
}

TEST_F(StringPrimitivesTest, SFindNotFound) {
    push_string("hello");
    push_string("xyz");
    run("sfind");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, -1);
}

// --- sreplace ---

TEST_F(StringPrimitivesTest, SReplace) {
    push_string("hello world");
    push_string("world");
    push_string("earth");
    run("sreplace");
    EXPECT_EQ(pop_string(), "hello earth");
}

TEST_F(StringPrimitivesTest, SReplaceMultiple) {
    push_string("aabaa");
    push_string("a");
    push_string("x");
    run("sreplace");
    EXPECT_EQ(pop_string(), "xxbxx");
}

// --- ssplit ---

TEST_F(StringPrimitivesTest, SSplit) {
    push_string("a,b,c");
    push_string(",");
    run("ssplit");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Array);
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 3u);

    Value v;
    arr->get(0, v);
    EXPECT_EQ(v.as_string()->view(), "a");
    v.release();
    arr->get(1, v);
    EXPECT_EQ(v.as_string()->view(), "b");
    v.release();
    arr->get(2, v);
    EXPECT_EQ(v.as_string()->view(), "c");
    v.release();

    arr->release();
}

// --- sjoin ---

TEST_F(StringPrimitivesTest, SJoin) {
    // Build array manually
    auto* arr = new HeapArray();
    arr->push_back(Value::from(HeapString::create("a")));
    arr->push_back(Value::from(HeapString::create("b")));
    arr->push_back(Value::from(HeapString::create("c")));
    ctx().data_stack().push(Value::from(arr));
    push_string(",");
    run("sjoin");
    EXPECT_EQ(pop_string(), "a,b,c");
}

// --- sregex-find ---

TEST_F(StringPrimitivesTest, SRegexFind) {
    push_string("hello 42 world");
    push_string("\\d+");
    run("sregex-find");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, 6);
}

TEST_F(StringPrimitivesTest, SRegexFindNoMatch) {
    push_string("hello world");
    push_string("\\d+");
    run("sregex-find");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->as_int, -1);
}

// --- sregex-replace ---

TEST_F(StringPrimitivesTest, SRegexReplace) {
    push_string("foo 123 bar 456");
    push_string("\\d+");
    push_string("NUM");
    run("sregex-replace");
    EXPECT_EQ(pop_string(), "foo NUM bar NUM");
}

// --- sregex-search ---

TEST_F(StringPrimitivesTest, SRegexSearchBasic) {
    push_string("hello 42 world");
    push_string("(\\d+)");
    run("sregex-search");
    // Should push: match-array, true
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto arr_val = ctx().data_stack().pop();
    ASSERT_TRUE(arr_val.has_value());
    ASSERT_EQ(arr_val->type, Value::Type::Array);
    auto* arr = arr_val->as_array();
    ASSERT_EQ(arr->length(), 2u);  // full match + 1 group
    Value v;
    ASSERT_TRUE(arr->get(0, v));
    EXPECT_EQ(std::string(v.as_string()->view()), "42");
    v.release();
    ASSERT_TRUE(arr->get(1, v));
    EXPECT_EQ(std::string(v.as_string()->view()), "42");
    v.release();
    arr->release();
}

TEST_F(StringPrimitivesTest, SRegexSearchNoMatch) {
    push_string("hello world");
    push_string("\\d+");
    run("sregex-search");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

TEST_F(StringPrimitivesTest, SRegexSearchMultipleGroups) {
    push_string("2026-03-02");
    push_string("(\\d{4})-(\\d{2})-(\\d{2})");
    run("sregex-search");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto arr_val = ctx().data_stack().pop();
    auto* arr = arr_val->as_array();
    ASSERT_EQ(arr->length(), 4u);  // full + 3 groups
    Value v;
    ASSERT_TRUE(arr->get(0, v));
    EXPECT_EQ(std::string(v.as_string()->view()), "2026-03-02");
    v.release();
    ASSERT_TRUE(arr->get(1, v));
    EXPECT_EQ(std::string(v.as_string()->view()), "2026");
    v.release();
    ASSERT_TRUE(arr->get(2, v));
    EXPECT_EQ(std::string(v.as_string()->view()), "03");
    v.release();
    ASSERT_TRUE(arr->get(3, v));
    EXPECT_EQ(std::string(v.as_string()->view()), "02");
    v.release();
    arr->release();
}

TEST_F(StringPrimitivesTest, SRegexSearchNoGroups) {
    push_string("abc123");
    push_string("\\d+");
    run("sregex-search");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto arr_val = ctx().data_stack().pop();
    auto* arr = arr_val->as_array();
    ASSERT_EQ(arr->length(), 1u);  // full match only
    Value v;
    ASSERT_TRUE(arr->get(0, v));
    EXPECT_EQ(std::string(v.as_string()->view()), "123");
    v.release();
    arr->release();
}

TEST_F(StringPrimitivesTest, SRegexSearchInvalidRegex) {
    push_string("hello");
    push_string("[invalid");
    run("sregex-search");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());  // treated as no match
}

TEST_F(StringPrimitivesTest, SRegexSearchTaintPropagation) {
    auto* hs = HeapString::create_tainted("hello 42 world");
    ctx().data_stack().push(Value::from(hs));
    push_string("(\\d+)");
    run("sregex-search");
    auto flag = ctx().data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto arr_val = ctx().data_stack().pop();
    auto* arr = arr_val->as_array();
    Value v;
    ASSERT_TRUE(arr->get(0, v));
    EXPECT_TRUE(v.as_string()->is_tainted());
    v.release();
    ASSERT_TRUE(arr->get(1, v));
    EXPECT_TRUE(v.as_string()->is_tainted());
    v.release();
    arr->release();
}

// --- sregex-match ---

TEST_F(StringPrimitivesTest, SRegexMatchFull) {
    push_string("2026-03-02");
    push_string("(\\d{4})-(\\d{2})-(\\d{2})");
    run("sregex-match");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto arr_val = ctx().data_stack().pop();
    auto* arr = arr_val->as_array();
    ASSERT_EQ(arr->length(), 4u);
    Value v;
    ASSERT_TRUE(arr->get(0, v));
    EXPECT_EQ(std::string(v.as_string()->view()), "2026-03-02");
    v.release();
    arr->release();
}

TEST_F(StringPrimitivesTest, SRegexMatchPartialFails) {
    // regex_match requires entire string to match
    push_string("hello 2026-03-02 world");
    push_string("(\\d{4})-(\\d{2})-(\\d{2})");
    run("sregex-match");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());  // partial match fails with regex_match
}

TEST_F(StringPrimitivesTest, SRegexMatchNoGroups) {
    push_string("hello");
    push_string("hello");
    run("sregex-match");
    auto flag = ctx().data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto arr_val = ctx().data_stack().pop();
    auto* arr = arr_val->as_array();
    ASSERT_EQ(arr->length(), 1u);  // full match, no groups
    Value v;
    ASSERT_TRUE(arr->get(0, v));
    EXPECT_EQ(std::string(v.as_string()->view()), "hello");
    v.release();
    arr->release();
}

TEST_F(StringPrimitivesTest, SRegexMatchTaintPropagation) {
    auto* hs = HeapString::create_tainted("hello");
    ctx().data_stack().push(Value::from(hs));
    push_string("(h)(ello)");
    run("sregex-match");
    auto flag = ctx().data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto arr_val = ctx().data_stack().pop();
    auto* arr = arr_val->as_array();
    ASSERT_EQ(arr->length(), 3u);
    for (size_t i = 0; i < arr->length(); ++i) {
        Value v;
        ASSERT_TRUE(arr->get(i, v));
        EXPECT_TRUE(v.as_string()->is_tainted());
        v.release();
    }
    arr->release();
}

// --- Refcount correctness ---

TEST_F(StringPrimitivesTest, TypeReleasesRef) {
    auto* hs = HeapString::create("test");
    hs->add_ref();  // extra ref to check
    ctx().data_stack().push(Value::from(hs));
    EXPECT_EQ(hs->refcount(), 2u);
    run("type");
    EXPECT_EQ(hs->refcount(), 1u);
    hs->release();
}

TEST_F(StringPrimitivesTest, SPlusReleasesBothInputs) {
    auto* s1 = HeapString::create("a");
    auto* s2 = HeapString::create("b");
    s1->add_ref();
    s2->add_ref();
    ctx().data_stack().push(Value::from(s1));
    ctx().data_stack().push(Value::from(s2));
    EXPECT_EQ(s1->refcount(), 2u);
    EXPECT_EQ(s2->refcount(), 2u);
    run("s+");
    EXPECT_EQ(s1->refcount(), 1u);
    EXPECT_EQ(s2->refcount(), 1u);
    // Result is a new string on stack
    auto result = pop_string();
    EXPECT_EQ(result, "ab");
    s1->release();
    s2->release();
}

// --- sprintf ---

TEST_F(StringPrimitivesTest, SprintfSimpleInt) {
    ctx().data_stack().push(Value(int64_t(42)));
    push_string("%d");
    run("sprintf");
    EXPECT_EQ(pop_string(), "42");
}

TEST_F(StringPrimitivesTest, SprintfNegativeInt) {
    ctx().data_stack().push(Value(int64_t(-7)));
    push_string("%d");
    run("sprintf");
    EXPECT_EQ(pop_string(), "-7");
}

TEST_F(StringPrimitivesTest, SprintfPaddedInt) {
    ctx().data_stack().push(Value(int64_t(42)));
    push_string("%08d");
    run("sprintf");
    EXPECT_EQ(pop_string(), "00000042");
}

TEST_F(StringPrimitivesTest, SprintfHexLower) {
    ctx().data_stack().push(Value(int64_t(255)));
    push_string("%x");
    run("sprintf");
    EXPECT_EQ(pop_string(), "ff");
}

TEST_F(StringPrimitivesTest, SprintfHexUpper) {
    ctx().data_stack().push(Value(int64_t(255)));
    push_string("%X");
    run("sprintf");
    EXPECT_EQ(pop_string(), "FF");
}

TEST_F(StringPrimitivesTest, SprintfOctal) {
    ctx().data_stack().push(Value(int64_t(8)));
    push_string("%o");
    run("sprintf");
    EXPECT_EQ(pop_string(), "10");
}

TEST_F(StringPrimitivesTest, SprintfUnsigned) {
    ctx().data_stack().push(Value(int64_t(-1)));
    push_string("%u");
    run("sprintf");
    // -1 as uint64_t = 18446744073709551615
    EXPECT_EQ(pop_string(), "18446744073709551615");
}

TEST_F(StringPrimitivesTest, SprintfSimpleFloat) {
    ctx().data_stack().push(Value(3.14));
    push_string("%f");
    run("sprintf");
    EXPECT_EQ(pop_string(), "3.140000");
}

TEST_F(StringPrimitivesTest, SprintfFloatPrecision) {
    ctx().data_stack().push(Value(3.14159));
    push_string("%.2f");
    run("sprintf");
    EXPECT_EQ(pop_string(), "3.14");
}

TEST_F(StringPrimitivesTest, SprintfScientific) {
    ctx().data_stack().push(Value(12345.0));
    push_string("%e");
    run("sprintf");
    auto result = pop_string();
    EXPECT_TRUE(result.find("1.234500e+04") != std::string::npos ||
                result.find("1.234500E+04") != std::string::npos);
}

TEST_F(StringPrimitivesTest, SprintfGFormat) {
    ctx().data_stack().push(Value(100.0));
    push_string("%g");
    run("sprintf");
    EXPECT_EQ(pop_string(), "100");
}

TEST_F(StringPrimitivesTest, SprintfString) {
    push_string("hello");
    push_string("%s");
    run("sprintf");
    EXPECT_EQ(pop_string(), "hello");
}

TEST_F(StringPrimitivesTest, SprintfStringPadded) {
    push_string("hi");
    push_string("%-10s!");
    run("sprintf");
    EXPECT_EQ(pop_string(), "hi        !");
}

TEST_F(StringPrimitivesTest, SprintfChar) {
    ctx().data_stack().push(Value(int64_t(65)));
    push_string("%c");
    run("sprintf");
    EXPECT_EQ(pop_string(), "A");
}

TEST_F(StringPrimitivesTest, SprintfPercentLiteral) {
    push_string("100%%");
    run("sprintf");
    EXPECT_EQ(pop_string(), "100%");
}

TEST_F(StringPrimitivesTest, SprintfMultiArg) {
    ctx().data_stack().push(Value(int64_t(42)));
    ctx().data_stack().push(Value(3.14));
    push_string("%d items at $%.2f");
    run("sprintf");
    EXPECT_EQ(pop_string(), "42 items at $3.14");
}

TEST_F(StringPrimitivesTest, SprintfThreeArgs) {
    push_string("Alice");
    ctx().data_stack().push(Value(int64_t(30)));
    ctx().data_stack().push(Value(1.75));
    push_string("%s is %d, height %.2f");
    run("sprintf");
    EXPECT_EQ(pop_string(), "Alice is 30, height 1.75");
}

TEST_F(StringPrimitivesTest, SprintfNoSpecifiers) {
    push_string("hello world");
    run("sprintf");
    EXPECT_EQ(pop_string(), "hello world");
}

TEST_F(StringPrimitivesTest, SprintfIntCoercedFromFloat) {
    ctx().data_stack().push(Value(3.7));
    push_string("%d");
    run("sprintf");
    EXPECT_EQ(pop_string(), "3");
}

TEST_F(StringPrimitivesTest, SprintfFloatCoercedFromInt) {
    ctx().data_stack().push(Value(int64_t(5)));
    push_string("%.1f");
    run("sprintf");
    EXPECT_EQ(pop_string(), "5.0");
}

TEST_F(StringPrimitivesTest, SprintfStringCoercedFromInt) {
    ctx().data_stack().push(Value(int64_t(42)));
    push_string("[%s]");
    run("sprintf");
    EXPECT_EQ(pop_string(), "[42]");
}

TEST_F(StringPrimitivesTest, SprintfUnderflowRestoresStack) {
    // Only push 1 arg but format needs 2
    ctx().data_stack().push(Value(int64_t(42)));
    push_string("%d %d");
    run("sprintf");
    // Should fail — stack restored with the integer and format string
    // The 42 and format string should be back on the stack
    EXPECT_GE(ctx().data_stack().size(), 1u);
    // Clean up stack
    while (ctx().data_stack().size() > 0) {
        auto v = ctx().data_stack().pop();
        if (v && v->type == Value::Type::String && v->as_ptr) v->as_string()->release();
    }
}

TEST_F(StringPrimitivesTest, SprintfEmptyFormat) {
    push_string("");
    run("sprintf");
    EXPECT_EQ(pop_string(), "");
}

TEST_F(StringPrimitivesTest, SprintfHexWithHash) {
    ctx().data_stack().push(Value(int64_t(255)));
    push_string("%#x");
    run("sprintf");
    EXPECT_EQ(pop_string(), "0xff");
}

TEST_F(StringPrimitivesTest, SprintfWidthAlignLeft) {
    ctx().data_stack().push(Value(int64_t(42)));
    push_string("%-5d|");
    run("sprintf");
    EXPECT_EQ(pop_string(), "42   |");
}
