// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/selection/selection_engine.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <cmath>
#include <map>

using namespace etil::selection;
using namespace etil::core;

// Helper: create a named WordImpl with a given weight
static WordImplPtr make_impl(const std::string& name, double weight) {
    auto id = Dictionary::next_id();
    WordImplPtr impl(new WordImpl(name, id));
    impl->set_weight(weight);
    return impl;
}

// ===================================================================
// Strategy::Latest
// ===================================================================

TEST(SelectionEngineTest, LatestReturnsLast) {
    SelectionEngine engine(Strategy::Latest);
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("a", 1.0));
    impls.push_back(make_impl("b", 1.0));
    impls.push_back(make_impl("c", 1.0));
    EXPECT_EQ(engine.select(impls)->name(), "c");
}

TEST(SelectionEngineTest, LatestIgnoresWeights) {
    SelectionEngine engine(Strategy::Latest);
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("a", 100.0));
    impls.push_back(make_impl("b", 0.001));
    EXPECT_EQ(engine.select(impls)->name(), "b");
}

TEST(SelectionEngineTest, EmptyReturnsNull) {
    SelectionEngine engine(Strategy::Latest);
    std::vector<WordImplPtr> impls;
    EXPECT_EQ(engine.select(impls), nullptr);
}

TEST(SelectionEngineTest, SingleImplReturnsIt) {
    SelectionEngine engine(Strategy::WeightedRandom);
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("only", 1.0));
    EXPECT_EQ(engine.select(impls)->name(), "only");
}

// ===================================================================
// Strategy::WeightedRandom
// ===================================================================

TEST(SelectionEngineTest, WeightedRespectsProbabilities) {
    SelectionEngine engine(Strategy::WeightedRandom);
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("low",  0.1));
    impls.push_back(make_impl("high", 0.9));

    std::map<std::string, int> counts;
    for (int i = 0; i < 10000; ++i) {
        auto* selected = engine.select(impls);
        counts[selected->name()]++;
    }
    // "high" should be selected ~90% of the time
    double high_ratio = static_cast<double>(counts["high"]) / 10000.0;
    EXPECT_GT(high_ratio, 0.85);
    EXPECT_LT(high_ratio, 0.95);
}

TEST(SelectionEngineTest, WeightedAllEqual) {
    SelectionEngine engine(Strategy::WeightedRandom);
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("a", 1.0));
    impls.push_back(make_impl("b", 1.0));
    impls.push_back(make_impl("c", 1.0));

    std::map<std::string, int> counts;
    for (int i = 0; i < 9000; ++i) {
        counts[engine.select(impls)->name()]++;
    }
    // Each should get ~33%
    for (auto& [name, count] : counts) {
        double ratio = static_cast<double>(count) / 9000.0;
        EXPECT_GT(ratio, 0.25);
        EXPECT_LT(ratio, 0.42);
    }
}

TEST(SelectionEngineTest, WeightedZeroWeightFallback) {
    SelectionEngine engine(Strategy::WeightedRandom);
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("a", 0.0));
    impls.push_back(make_impl("b", 0.0));
    // All zero weights: should fallback to last
    EXPECT_EQ(engine.select(impls)->name(), "b");
}

TEST(SelectionEngineTest, WeightedDominant) {
    SelectionEngine engine(Strategy::WeightedRandom);
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("weak", 0.001));
    impls.push_back(make_impl("strong", 1000.0));

    int strong_count = 0;
    for (int i = 0; i < 1000; ++i) {
        if (engine.select(impls)->name() == "strong") strong_count++;
    }
    EXPECT_GT(strong_count, 995);
}

// ===================================================================
// Strategy::EpsilonGreedy
// ===================================================================

TEST(SelectionEngineTest, EpsilonGreedyExploitsAtZero) {
    SelectionEngine engine(Strategy::EpsilonGreedy);
    engine.set_epsilon(0.0);  // Always exploit
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("best", 10.0));
    impls.push_back(make_impl("worst", 1.0));

    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(engine.select(impls)->name(), "best");
    }
}

TEST(SelectionEngineTest, EpsilonGreedyExploresAtOne) {
    SelectionEngine engine(Strategy::EpsilonGreedy);
    engine.set_epsilon(1.0);  // Always explore
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("a", 10.0));
    impls.push_back(make_impl("b", 1.0));

    std::map<std::string, int> counts;
    for (int i = 0; i < 10000; ++i) {
        counts[engine.select(impls)->name()]++;
    }
    // Should be roughly 50/50
    double a_ratio = static_cast<double>(counts["a"]) / 10000.0;
    EXPECT_GT(a_ratio, 0.40);
    EXPECT_LT(a_ratio, 0.60);
}

TEST(SelectionEngineTest, EpsilonGreedyMixed) {
    SelectionEngine engine(Strategy::EpsilonGreedy);
    engine.set_epsilon(0.1);
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("best", 10.0));
    impls.push_back(make_impl("other", 1.0));

    int best_count = 0;
    for (int i = 0; i < 10000; ++i) {
        if (engine.select(impls)->name() == "best") best_count++;
    }
    // Should pick "best" ~95% (90% exploit + 50% of 10% explore)
    double ratio = static_cast<double>(best_count) / 10000.0;
    EXPECT_GT(ratio, 0.90);
    EXPECT_LT(ratio, 0.99);
}

// ===================================================================
// Strategy::UCB1
// ===================================================================

TEST(SelectionEngineTest, UCB1PicksUntriedFirst) {
    SelectionEngine engine(Strategy::UCB1);
    std::vector<WordImplPtr> impls;
    auto tried = make_impl("tried", 1.0);
    tried->record_execution(std::chrono::nanoseconds(100), 0, true);
    impls.push_back(std::move(tried));
    impls.push_back(make_impl("untried", 1.0));

    EXPECT_EQ(engine.select(impls)->name(), "untried");
}

TEST(SelectionEngineTest, UCB1AllUntriedPicksFirst) {
    SelectionEngine engine(Strategy::UCB1);
    std::vector<WordImplPtr> impls;
    impls.push_back(make_impl("a", 1.0));
    impls.push_back(make_impl("b", 1.0));
    EXPECT_EQ(engine.select(impls)->name(), "a");
}

TEST(SelectionEngineTest, UCB1FavorsHighSuccessRate) {
    SelectionEngine engine(Strategy::UCB1);
    std::vector<WordImplPtr> impls;
    auto good = make_impl("good", 1.0);
    for (int i = 0; i < 100; ++i)
        good->record_execution(std::chrono::nanoseconds(100), 0, true);
    auto bad = make_impl("bad", 1.0);
    for (int i = 0; i < 100; ++i)
        bad->record_execution(std::chrono::nanoseconds(100), 0, false);
    impls.push_back(std::move(good));
    impls.push_back(std::move(bad));

    // With equal calls, good (100% success) should beat bad (0% success)
    int good_count = 0;
    for (int i = 0; i < 100; ++i) {
        if (engine.select(impls)->name() == "good") good_count++;
    }
    EXPECT_GT(good_count, 90);
}

// ===================================================================
// Configuration
// ===================================================================

TEST(SelectionEngineTest, SetStrategy) {
    SelectionEngine engine;
    EXPECT_EQ(engine.strategy(), Strategy::Latest);
    engine.set_strategy(Strategy::WeightedRandom);
    EXPECT_EQ(engine.strategy(), Strategy::WeightedRandom);
}

TEST(SelectionEngineTest, SetEpsilon) {
    SelectionEngine engine;
    engine.set_epsilon(0.5);
    EXPECT_DOUBLE_EQ(engine.epsilon(), 0.5);
}

// ===================================================================
// Dictionary::select integration
// ===================================================================

TEST(SelectionEngineTest, DictionarySelectLatest) {
    Dictionary dict;
    dict.register_word("foo", make_impl("foo_v1", 1.0));
    dict.register_word("foo", make_impl("foo_v2", 1.0));

    SelectionEngine engine(Strategy::Latest);
    auto result = dict.select("foo", engine);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)->name(), "foo_v2");
}

TEST(SelectionEngineTest, DictionarySelectNotFound) {
    Dictionary dict;
    SelectionEngine engine(Strategy::Latest);
    auto result = dict.select("nonexistent", engine);
    EXPECT_FALSE(result.has_value());
}

// ===================================================================
// Interpreter integration (primitives)
// ===================================================================

class SelectionPrimitivesTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    Interpreter interp{dict, out, err};
    SelectionEngine engine;

    void SetUp() override {
        register_primitives(dict);
        interp.context().set_selection_engine(&engine);
    }

    ExecutionContext& ctx() { return interp.context(); }
};

TEST_F(SelectionPrimitivesTest, SelectStrategyWeighted) {
    interp.interpret_line("1 select-strategy");
    EXPECT_EQ(engine.strategy(), Strategy::WeightedRandom);
}

TEST_F(SelectionPrimitivesTest, SelectStrategyUCB1) {
    interp.interpret_line("3 select-strategy");
    EXPECT_EQ(engine.strategy(), Strategy::UCB1);
}

TEST_F(SelectionPrimitivesTest, SelectOff) {
    engine.set_strategy(Strategy::WeightedRandom);
    interp.interpret_line("select-off");
    EXPECT_EQ(engine.strategy(), Strategy::Latest);
}

TEST_F(SelectionPrimitivesTest, SelectEpsilon) {
    interp.interpret_line("0.25 select-epsilon");
    EXPECT_DOUBLE_EQ(engine.epsilon(), 0.25);
}

TEST_F(SelectionPrimitivesTest, SelectInvalidStrategy) {
    interp.interpret_line("99 select-strategy");
    EXPECT_NE(err.str().find("invalid strategy"), std::string::npos);
}

TEST_F(SelectionPrimitivesTest, MultiImplWeightedSelection) {
    // Register two implementations of "greet"
    interp.interpret_line(": greet .\" hello\" cr ;");
    interp.interpret_line(": greet .\" hi\" cr ;");
    // Set weights
    auto impls = dict.get_implementations("greet");
    ASSERT_TRUE(impls.has_value());
    ASSERT_EQ(impls->size(), 2u);
    (*impls)[0]->set_weight(0.0);  // never pick first
    (*impls)[1]->set_weight(1.0);  // always pick second

    engine.set_strategy(Strategy::WeightedRandom);
    out.str("");
    interp.interpret_line("greet");
    EXPECT_EQ(out.str(), "hi\n");
}
