// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/word_impl.hpp"
#include <gtest/gtest.h>

using namespace etil::core;

TEST(WordImplTest, Creation) {
    WordImpl word("test_word", 1);
    
    EXPECT_EQ(word.id(), 1);
    EXPECT_EQ(word.name(), "test_word");
    EXPECT_EQ(word.generation(), 0);
    EXPECT_DOUBLE_EQ(word.weight(), 1.0);
}

TEST(WordImplTest, PerformanceTracking) {
    WordImpl word("perf_word", 2);
    
    word.record_execution(std::chrono::nanoseconds(100), 1024, true);
    word.record_execution(std::chrono::nanoseconds(200), 2048, true);
    word.record_execution(std::chrono::nanoseconds(150), 1536, false);
    
    EXPECT_EQ(word.success_count(), 2);
    EXPECT_EQ(word.failure_count(), 1);
    EXPECT_DOUBLE_EQ(word.success_rate(), 2.0 / 3.0);
    
    auto& profile = word.profile();
    EXPECT_EQ(profile.total_calls, 3);
    EXPECT_DOUBLE_EQ(profile.mean_duration_ns(), 150.0);
}

TEST(WordImplTest, TypeSignature) {
    TypeSignature sig1;
    sig1.inputs = {TypeSignature::Type::Integer, TypeSignature::Type::Float};
    sig1.outputs = {TypeSignature::Type::Integer};
    
    TypeSignature sig2;
    sig2.inputs = {TypeSignature::Type::Integer, TypeSignature::Type::Float};
    sig2.outputs = {TypeSignature::Type::Integer};
    
    EXPECT_TRUE(sig1.matches(sig2));
    
    TypeSignature sig3;
    sig3.inputs = {TypeSignature::Type::Integer};
    sig3.outputs = {TypeSignature::Type::Integer};
    
    EXPECT_FALSE(sig1.matches(sig3));
}

TEST(WordImplTest, SmartPointer) {
    WordImpl* raw = new WordImpl("ptr_test", 3);
    
    {
        WordImplPtr ptr1(raw);
        EXPECT_EQ(ptr1->id(), 3);
        
        {
            WordImplPtr ptr2 = ptr1;  // Copy
            EXPECT_EQ(ptr2->id(), 3);
        } // ptr2 destroyed, but raw still alive
        
        EXPECT_EQ(ptr1->id(), 3);
    } // ptr1 destroyed, raw should be deleted
    
    // Can't verify deletion, but shouldn't crash
}

TEST(WordImplTest, Mutations) {
    WordImpl word("mutate_test", 4);
    
    word.add_mutation(MutationHistory::MutationType::Inline, "Inlined helper function");
    word.add_mutation(MutationHistory::MutationType::Vectorize, "Vectorized loop");
    
    const auto& mutations = word.mutations();
    EXPECT_EQ(mutations.mutations.size(), 2);
    EXPECT_EQ(mutations.mutations[0], MutationHistory::MutationType::Inline);
    EXPECT_EQ(mutations.mutations[1], MutationHistory::MutationType::Vectorize);
}
