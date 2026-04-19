// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Phase 4c — evolution engine publishes generation.start /
// generation.end lifecycle events onto etil.evolution.**. Tests
// verify that evolve_word emits the expected events with word and
// generation tags, and that emission is a no-op when channels is null.

#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include "etil/evolution/evolution_engine.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"

#include <memory>
#include <sstream>

#include <gtest/gtest.h>

using etil::core::Dictionary;
using etil::core::Interpreter;
using etil::core::register_primitives;
using etil::core::Value;
using etil::evolution::EvolutionConfig;
using etil::evolution::EvolutionEngine;
using etil::evolution::TestCase;
using etil::manifold::make_default_channel_service;
using etil::manifold::make_test_capture_sink;
using etil::manifold::RouteSpec;

TEST(EvolutionChannels, NoChannelsNoEmissions) {
    Dictionary dict;
    register_primitives(dict);
    EvolutionConfig cfg;
    EvolutionEngine engine(cfg, dict);
    EXPECT_EQ(engine.channels(), nullptr);
    // Calling with no tests returns 0 — no emissions expected either.
    engine.evolve_word("non-existent-word");
    SUCCEED();
}

TEST(EvolutionChannels, SetChannelsExposesService) {
    Dictionary dict;
    register_primitives(dict);
    EvolutionConfig cfg;
    EvolutionEngine engine(cfg, dict);
    auto svc = make_default_channel_service();
    engine.set_channels(svc.get());
    EXPECT_EQ(engine.channels(), svc.get());
    engine.set_channels(nullptr);
    EXPECT_EQ(engine.channels(), nullptr);
}

TEST(EvolutionChannels, EvolveWordEmitsStartAndEnd) {
    Dictionary dict;
    register_primitives(dict);

    // Compile a trivial bytecode word so evolve_word has something to
    // work with.
    std::ostringstream out;
    Interpreter interp{dict, out};
    interp.interpret_line(": double dup + ;");

    EvolutionConfig cfg;
    cfg.generation_size = 2;
    EvolutionEngine engine(cfg, dict);

    auto svc = make_default_channel_service();
    engine.set_channels(svc.get());

    auto cap_start = make_test_capture_sink();
    auto cap_end = make_test_capture_sink();
    RouteSpec s;
    s.channel_pattern = "etil.evolution.generation.start";
    s.sink = cap_start;
    svc->add_route(std::move(s));
    RouteSpec e;
    e.channel_pattern = "etil.evolution.generation.end";
    e.sink = cap_end;
    svc->add_route(std::move(e));

    std::vector<TestCase> tests;
    tests.push_back({{Value(int64_t(3))}, {Value(int64_t(6))}});
    tests.push_back({{Value(int64_t(5))}, {Value(int64_t(10))}});
    engine.register_tests("double", std::move(tests));

    engine.evolve_word("double");

    EXPECT_GE(cap_start->size(), 1u);
    EXPECT_GE(cap_end->size(), 1u);
    auto start_msgs = cap_start->captured();
    EXPECT_EQ(start_msgs.front().tags.at("word"), "double");
    EXPECT_TRUE(start_msgs.front().tags.contains("generation"));

    auto end_msgs = cap_end->captured();
    EXPECT_EQ(end_msgs.front().tags.at("word"), "double");
    EXPECT_TRUE(end_msgs.front().tags.contains("children"));
    EXPECT_TRUE(end_msgs.front().tags.contains("best_fitness"));
}
