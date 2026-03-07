// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/dictionary.hpp"
#include <gtest/gtest.h>

using namespace etil::core;

TEST(DictionaryTest, RegisterAndLookup) {
    Dictionary dict;
    auto id = Dictionary::next_id();
    WordImplPtr impl(new WordImpl("test_word", id));
    dict.register_word("DUP", std::move(impl));

    auto result = dict.lookup("DUP");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get()->id(), id);
}

TEST(DictionaryTest, LookupNotFound) {
    Dictionary dict;
    auto result = dict.lookup("NONEXISTENT");
    ASSERT_FALSE(result.has_value());
}

TEST(DictionaryTest, MultipleImplementations) {
    Dictionary dict;
    auto id1 = Dictionary::next_id();
    auto id2 = Dictionary::next_id();
    dict.register_word("SORT", WordImplPtr(new WordImpl("sort_v1", id1)));
    dict.register_word("SORT", WordImplPtr(new WordImpl("sort_v2", id2)));

    auto result = dict.get_implementations("SORT");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 2u);
    EXPECT_EQ((*result)[0]->id(), id1);
    EXPECT_EQ((*result)[1]->id(), id2);

    // lookup returns the latest (most recently registered) implementation
    auto latest = dict.lookup("SORT");
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->get()->id(), id2);
}

TEST(DictionaryTest, ConceptCount) {
    Dictionary dict;
    EXPECT_EQ(dict.concept_count(), 0u);

    dict.register_word("A", WordImplPtr(new WordImpl("a", Dictionary::next_id())));
    EXPECT_EQ(dict.concept_count(), 1u);

    dict.register_word("B", WordImplPtr(new WordImpl("b", Dictionary::next_id())));
    EXPECT_EQ(dict.concept_count(), 2u);

    // Adding another impl to "A" doesn't increase concept count
    dict.register_word("A", WordImplPtr(new WordImpl("a2", Dictionary::next_id())));
    EXPECT_EQ(dict.concept_count(), 2u);
}
