// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/signature_index.hpp"
#include <gtest/gtest.h>
#include <algorithm>

using namespace etil::evolution;
using namespace etil::core;
using T = TypeSignature::Type;

// --- type_compatible() direct tests ---

TEST(TypeCompatibleTest, ExactMatch) {
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Integer, T::Integer));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Float, T::Float));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::String, T::String));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Boolean, T::Boolean));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Array, T::Array));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Matrix, T::Matrix));
}

TEST(TypeCompatibleTest, IntegerToFloatWidening) {
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Integer, T::Float));
}

TEST(TypeCompatibleTest, FloatToIntegerNarrowing) {
    EXPECT_FALSE(SignatureIndex::type_compatible(T::Float, T::Integer));
}

TEST(TypeCompatibleTest, BooleanToIntegerUndefined) {
    EXPECT_FALSE(SignatureIndex::type_compatible(T::Boolean, T::Integer));
}

TEST(TypeCompatibleTest, UnknownStackPermissive) {
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Unknown, T::Float));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Unknown, T::Integer));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Unknown, T::String));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Unknown, T::Boolean));
}

TEST(TypeCompatibleTest, WordAcceptsUnknownPermissive) {
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Integer, T::Unknown));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Float, T::Unknown));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::String, T::Unknown));
    EXPECT_TRUE(SignatureIndex::type_compatible(T::Array, T::Unknown));
}

TEST(TypeCompatibleTest, DifferentConcreteTypes) {
    EXPECT_FALSE(SignatureIndex::type_compatible(T::Integer, T::String));
    EXPECT_FALSE(SignatureIndex::type_compatible(T::String, T::Integer));
    EXPECT_FALSE(SignatureIndex::type_compatible(T::Array, T::Integer));
    EXPECT_FALSE(SignatureIndex::type_compatible(T::Boolean, T::Float));
    EXPECT_FALSE(SignatureIndex::type_compatible(T::Matrix, T::String));
}

TEST(TypeCompatibleTest, BooleanToFloatIncompatible) {
    EXPECT_FALSE(SignatureIndex::type_compatible(T::Boolean, T::Float));
}

// --- find_type_compatible() integration tests ---

class SignatureIndexTypeTest : public ::testing::Test {
protected:
    Dictionary dict;
    SignatureIndex index;

    // Helper: register a word with given input/output types
    void add_word(const std::string& name,
                  const std::vector<T>& inputs,
                  const std::vector<T>& outputs) {
        auto impl = WordImplPtr(new WordImpl(name, Dictionary::next_id()));
        TypeSignature sig;
        sig.inputs = inputs;
        sig.outputs = outputs;
        impl->set_signature(std::move(sig));
        dict.register_word(name, std::move(impl));
    }

    void SetUp() override {
        // + : (Unknown, Unknown → Unknown) — polymorphic arithmetic
        add_word("+", {T::Unknown, T::Unknown}, {T::Unknown});
        // copy-file : (String, String → Integer) — concrete types
        add_word("copy-file", {T::String, T::String}, {T::Integer});
        // pow : (Unknown, Unknown → Float) — polymorphic inputs, concrete output
        add_word("pow", {T::Unknown, T::Unknown}, {T::Float});
        // lshift : (Integer, Integer → Integer) — concrete integer
        add_word("lshift", {T::Integer, T::Integer}, {T::Integer});
        // and : (Boolean, Boolean → Boolean) — boolean logic
        add_word("and", {T::Boolean, T::Boolean}, {T::Boolean});
        // s+ : (String, String → String) — string concatenation
        add_word("s+", {T::String, T::String}, {T::String});

        index.rebuild(dict);
    }

    bool contains(const std::vector<std::string>& v, const std::string& name) {
        return std::find(v.begin(), v.end(), name) != v.end();
    }
};

TEST_F(SignatureIndexTypeTest, IntegerIntegerStack) {
    // Integer stack: + (Unknown accepts), pow (Unknown accepts), lshift (Integer exact)
    // Excludes: copy-file (String), and (Boolean), s+ (String)
    auto result = index.find_type_compatible(2, 1, {T::Integer, T::Integer});
    EXPECT_TRUE(contains(result, "+"));
    EXPECT_TRUE(contains(result, "pow"));
    EXPECT_TRUE(contains(result, "lshift"));
    EXPECT_FALSE(contains(result, "copy-file"));
    EXPECT_FALSE(contains(result, "and"));
    EXPECT_FALSE(contains(result, "s+"));
}

TEST_F(SignatureIndexTypeTest, StringStringStack) {
    // String stack: + (Unknown accepts), copy-file (String exact), pow (Unknown),
    //              s+ (String exact)
    // Excludes: lshift (Integer), and (Boolean)
    auto result = index.find_type_compatible(2, 1, {T::String, T::String});
    EXPECT_TRUE(contains(result, "+"));
    EXPECT_TRUE(contains(result, "copy-file"));
    EXPECT_TRUE(contains(result, "pow"));
    EXPECT_TRUE(contains(result, "s+"));
    EXPECT_FALSE(contains(result, "lshift"));
    EXPECT_FALSE(contains(result, "and"));
}

TEST_F(SignatureIndexTypeTest, IntegerFloatMixed) {
    // Integer, Float stack: + (Unknown), pow (Unknown)
    // lshift wants (Integer, Integer) — Float→Integer is narrowing, excluded
    auto result = index.find_type_compatible(2, 1, {T::Integer, T::Float});
    EXPECT_TRUE(contains(result, "+"));
    EXPECT_TRUE(contains(result, "pow"));
    EXPECT_FALSE(contains(result, "lshift"));
    EXPECT_FALSE(contains(result, "copy-file"));
}

TEST_F(SignatureIndexTypeTest, FloatFloatStack) {
    // Float stack: + (Unknown accepts), pow (Unknown accepts)
    // Excludes: lshift (Integer — Float→Integer narrowing), copy-file (String), and (Boolean)
    auto result = index.find_type_compatible(2, 1, {T::Float, T::Float});
    EXPECT_TRUE(contains(result, "+"));
    EXPECT_TRUE(contains(result, "pow"));
    EXPECT_FALSE(contains(result, "lshift"));
    EXPECT_FALSE(contains(result, "copy-file"));
    EXPECT_FALSE(contains(result, "and"));
}

TEST_F(SignatureIndexTypeTest, BooleanBooleanStack) {
    // Boolean stack: + (Unknown accepts), and (Boolean exact), pow (Unknown)
    // Excludes: lshift (Integer — Boolean→Integer undefined), copy-file (String), s+ (String)
    auto result = index.find_type_compatible(2, 1, {T::Boolean, T::Boolean});
    EXPECT_TRUE(contains(result, "+"));
    EXPECT_TRUE(contains(result, "and"));
    EXPECT_TRUE(contains(result, "pow"));
    EXPECT_FALSE(contains(result, "lshift"));
    EXPECT_FALSE(contains(result, "copy-file"));
    EXPECT_FALSE(contains(result, "s+"));
}

TEST_F(SignatureIndexTypeTest, UnknownUnknownFallback) {
    // All Unknown: includes everything (permissive)
    auto result = index.find_type_compatible(2, 1, {T::Unknown, T::Unknown});
    EXPECT_EQ(result.size(), 6u);
}

TEST_F(SignatureIndexTypeTest, EmptyStackTypesFallback) {
    // Empty stack types: falls back to depth-only
    auto result = index.find_type_compatible(2, 1, {});
    EXPECT_EQ(result.size(), 6u);
}

TEST_F(SignatureIndexTypeTest, FallbackWhenNoTypeMatch) {
    // Register a word that only accepts Matrix — no (2,1) words accept Matrix
    add_word("mat-op", {T::Matrix, T::Matrix}, {T::Matrix});
    index.rebuild(dict);
    // Matrix stack: mat-op (exact), + (Unknown), pow (Unknown)
    // But let's test with a depth that has no type-compatible: use (1,1) depth with Matrix
    add_word("negate", {T::Integer}, {T::Integer});
    add_word("abs", {T::Integer}, {T::Integer});
    index.rebuild(dict);
    // Matrix on (1,1): negate wants Integer, abs wants Integer — Matrix→Integer is false
    // No type-compatible match → should fall back to depth-only
    auto result = index.find_type_compatible(1, 1, {T::Matrix});
    // Fallback returns all (1,1) words
    EXPECT_TRUE(contains(result, "negate"));
    EXPECT_TRUE(contains(result, "abs"));
}

TEST_F(SignatureIndexTypeTest, IntegerWideningToFloat) {
    // Word wanting Float input — Integer on stack should be compatible (widening)
    add_word("sin", {T::Float}, {T::Float});
    index.rebuild(dict);
    auto result = index.find_type_compatible(1, 1, {T::Integer});
    EXPECT_TRUE(contains(result, "sin"));
}

TEST_F(SignatureIndexTypeTest, FloatNarrowingExcluded) {
    // Word wanting Integer input — Float on stack should be excluded (narrowing)
    // Add a compatible (1,1) word so fallback doesn't mask the exclusion
    add_word("bit-not", {T::Integer}, {T::Integer});
    add_word("sqrt", {T::Float}, {T::Float});
    index.rebuild(dict);
    auto result = index.find_type_compatible(1, 1, {T::Float});
    EXPECT_TRUE(contains(result, "sqrt"));
    EXPECT_FALSE(contains(result, "bit-not"));
}
