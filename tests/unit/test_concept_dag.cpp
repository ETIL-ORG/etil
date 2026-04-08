// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/concept_dag.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/compiled_body.hpp"
#include "etil/core/word_impl.hpp"

#include <gtest/gtest.h>
#include <random>
#include <sstream>

using namespace etil::core;
using namespace etil::evolution;

// Helper: create a bytecode impl that calls the given words
static WordImplPtr make_calling_impl(const std::vector<std::string>& calls) {
    auto* impl = new WordImpl("test", Dictionary::next_id());
    auto bc = std::make_shared<ByteCode>();
    for (const auto& name : calls) {
        Instruction instr;
        instr.op = Instruction::Op::Call;
        instr.word_name = name;
        bc->instructions().push_back(instr);
    }
    impl->set_bytecode(bc);
    return WordImplPtr(impl);
}

// Helper: create a native (primitive) impl
static WordImplPtr make_native_impl() {
    auto* impl = new WordImpl("test", Dictionary::next_id());
    impl->set_native_code([](ExecutionContext&) { return true; });
    return WordImplPtr(impl);
}

class ConceptDAGTest : public ::testing::Test {
protected:
    Dictionary dict;
};

TEST_F(ConceptDAGTest, BuildSimpleChain) {
    // A -> B -> C (linear chain, C is primitive)
    dict.register_word("C", make_native_impl());
    dict.register_word("B", make_calling_impl({"C"}));
    dict.register_word("A", make_calling_impl({"B"}));

    ConceptDAG dag;
    dag.build("A", dict);

    EXPECT_EQ(dag.root(), "A");
    EXPECT_EQ(dag.size(), 3u);

    auto* a = dag.node("A");
    auto* b = dag.node("B");
    auto* c = dag.node("C");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);

    EXPECT_EQ(a->depth, 0u);
    EXPECT_EQ(b->depth, 1u);
    EXPECT_EQ(c->depth, 2u);

    EXPECT_FALSE(a->opaque);
    EXPECT_FALSE(b->opaque);
    EXPECT_TRUE(c->opaque);  // native = opaque

    // Topological order: A first (root), then B, then C
    const auto& topo = dag.topo_order();
    ASSERT_EQ(topo.size(), 3u);
    EXPECT_EQ(topo[0], "A");
}

TEST_F(ConceptDAGTest, BuildDiamond) {
    // A -> B, A -> C, B -> D, C -> D
    dict.register_word("D", make_native_impl());
    dict.register_word("B", make_calling_impl({"D"}));
    dict.register_word("C", make_calling_impl({"D"}));
    dict.register_word("A", make_calling_impl({"B", "C"}));

    ConceptDAG dag;
    dag.build("A", dict);

    EXPECT_EQ(dag.size(), 4u);

    auto* d = dag.node("D");
    ASSERT_NE(d, nullptr);
    // D is reachable via both B (depth 2) and C (depth 2) — shallowest wins
    EXPECT_EQ(d->depth, 2u);
    EXPECT_TRUE(d->opaque);  // native

    auto* a = dag.node("A");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->children.size(), 2u);
}

TEST_F(ConceptDAGTest, DetectRecursion) {
    // A -> B -> A (cycle)
    dict.register_word("A", make_calling_impl({"B"}));
    dict.register_word("B", make_calling_impl({"A"}));

    ConceptDAG dag;
    dag.build("A", dict);

    // A is the root (depth 0, not opaque)
    // B calls A — A is already an ancestor, so the second encounter is opaque
    auto* a = dag.node("A");
    auto* b = dag.node("B");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_FALSE(a->opaque);  // root is not marked opaque
    EXPECT_FALSE(b->opaque);  // B has bytecode, not recursive itself

    // B's children include A, but when scanning B we find A is an ancestor
    // The recursive A reference should be in the DAG as opaque at depth 2
    // Actually, A is already in the DAG at depth 0 so it won't be re-added.
    // The cycle is broken by the "already in DAG" check.
    EXPECT_EQ(dag.size(), 2u);
}

TEST_F(ConceptDAGTest, PrimitivesAreOpaque) {
    // A calls "+" which is a native primitive
    dict.register_word("+", make_native_impl());
    dict.register_word("A", make_calling_impl({"+"}));

    ConceptDAG dag;
    dag.build("A", dict);

    auto* plus = dag.node("+");
    ASSERT_NE(plus, nullptr);
    EXPECT_TRUE(plus->opaque);
}

TEST_F(ConceptDAGTest, EmptyConcept) {
    // Root has no impls
    ConceptDAG dag;
    dag.build("nonexistent", dict);

    EXPECT_EQ(dag.size(), 1u);  // root node exists but is opaque
    auto* n = dag.node("nonexistent");
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(n->opaque);
}

TEST_F(ConceptDAGTest, EvolvableConcepts) {
    // A -> B -> C (C is primitive)
    dict.register_word("C", make_native_impl());
    dict.register_word("B", make_calling_impl({"C"}));
    dict.register_word("A", make_calling_impl({"B"}));

    ConceptDAG dag;
    dag.build("A", dict);

    auto evolvable = dag.evolvable_concepts();
    // B is evolvable (has bytecode, not root, not opaque)
    // A is root (excluded), C is opaque (excluded)
    ASSERT_EQ(evolvable.size(), 1u);
    EXPECT_EQ(evolvable[0], "B");
}

TEST_F(ConceptDAGTest, SelectForEvolution) {
    // A -> B, A -> C (both bytecode, not primitive)
    dict.register_word("leaf", make_native_impl());
    dict.register_word("B", make_calling_impl({"leaf"}));
    dict.register_word("C", make_calling_impl({"leaf"}));
    dict.register_word("A", make_calling_impl({"B", "C"}));

    ConceptDAG dag;
    dag.build("A", dict);

    // Set B contribution high, C contribution low
    dag.node("B")->contribution = 0.9;
    dag.node("C")->contribution = 0.1;

    // Over many selections, B should be picked much more often
    std::mt19937_64 rng(42);
    size_t b_count = 0, c_count = 0;
    for (int i = 0; i < 1000; ++i) {
        auto selected = dag.select_for_evolution(rng);
        if (selected == "B") b_count++;
        else if (selected == "C") c_count++;
    }

    // B should be picked ~90% of the time
    EXPECT_GT(b_count, 800u);
    EXPECT_LT(c_count, 200u);
}

TEST_F(ConceptDAGTest, ResetClearsStats) {
    dict.register_word("leaf", make_native_impl());
    dict.register_word("B", make_calling_impl({"leaf"}));
    dict.register_word("A", make_calling_impl({"B"}));

    ConceptDAG dag;
    dag.build("A", dict);

    // Modify stats
    dag.node("B")->contribution = 0.5;
    dag.node("B")->stats.generations_evolved = 10;
    dag.node("B")->stats.record_fitness(0.8);

    dag.reset();

    EXPECT_DOUBLE_EQ(dag.node("B")->contribution, 1.0);
    EXPECT_EQ(dag.node("B")->stats.generations_evolved, 0u);
    EXPECT_EQ(dag.node("B")->stats.eval_count, 0u);
}

TEST_F(ConceptDAGTest, NormalizeContributions) {
    dict.register_word("leaf", make_native_impl());
    dict.register_word("B", make_calling_impl({"leaf"}));
    dict.register_word("C", make_calling_impl({"leaf"}));
    dict.register_word("A", make_calling_impl({"B", "C"}));

    ConceptDAG dag;
    dag.build("A", dict);

    dag.node("B")->contribution = 3.0;
    dag.node("C")->contribution = 1.0;
    dag.normalize_contributions();

    EXPECT_NEAR(dag.node("B")->contribution, 0.75, 0.01);
    EXPECT_NEAR(dag.node("C")->contribution, 0.25, 0.01);
}

TEST_F(ConceptDAGTest, DumpFormat) {
    dict.register_word("leaf", make_native_impl());
    dict.register_word("sq", make_calling_impl({"leaf"}));
    dict.register_word("fn", make_calling_impl({"sq"}));

    ConceptDAG dag;
    dag.build("fn", dict);

    std::ostringstream out;
    dag.dump(out);
    std::string output = out.str();

    EXPECT_NE(output.find("ConceptDAG: fn"), std::string::npos);
    EXPECT_NE(output.find("[root]"), std::string::npos);
    EXPECT_NE(output.find("sq"), std::string::npos);
    EXPECT_NE(output.find("[opaque]"), std::string::npos);
    EXPECT_NE(output.find("depth="), std::string::npos);
    EXPECT_NE(output.find("contrib="), std::string::npos);
}

TEST_F(ConceptDAGTest, RecordFitnessStats) {
    ConceptNodeStats stats;
    stats.record_fitness(0.5);
    stats.record_fitness(0.8);
    stats.record_fitness(0.3);

    EXPECT_EQ(stats.eval_count, 3u);
    EXPECT_DOUBLE_EQ(stats.best_fitness, 0.8);
    EXPECT_DOUBLE_EQ(stats.worst_fitness, 0.3);
    EXPECT_NEAR(stats.mean_fitness, (0.5 + 0.8 + 0.3) / 3.0, 1e-9);
    // Variance should be non-zero
    EXPECT_GT(stats.fitness_variance, 0.0);
}

TEST_F(ConceptDAGTest, MaxDepth) {
    // A -> B -> C -> D (D is primitive)
    dict.register_word("D", make_native_impl());
    dict.register_word("C", make_calling_impl({"D"}));
    dict.register_word("B", make_calling_impl({"C"}));
    dict.register_word("A", make_calling_impl({"B"}));

    ConceptDAG dag;
    dag.build("A", dict);

    EXPECT_EQ(dag.max_depth(), 3u);
}
