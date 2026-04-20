// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Phase 4d — EvolveLogger absorption: verify log()/detail() still
// write to the legacy file AND publish onto etil.evolution.<cat>
// subchannels when a ChannelService is attached.

#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include "etil/evolution/evolution_engine.hpp"
#include "etil/evolution/evolve_logger.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include <gtest/gtest.h>

using etil::core::Dictionary;
using etil::core::Interpreter;
using etil::core::register_primitives;
using etil::core::Value;
using etil::evolution::EvolutionConfig;
using etil::evolution::EvolutionEngine;
using etil::evolution::EvolveLogCategory;
using etil::evolution::EvolveLogger;
using etil::evolution::EvolveLogLevel;
using etil::evolution::TestCase;
using etil::manifold::make_default_channel_service;
using etil::manifold::make_test_capture_sink;
using etil::manifold::RouteSpec;

// ---------------------------------------------------------------------------
// category_channel_suffix — static mapping table
// ---------------------------------------------------------------------------

TEST(EvolveLoggerAbsorb, CategoryChannelSuffixStable) {
    EXPECT_STREQ(EvolveLogger::category_channel_suffix(
                     EvolveLogCategory::Engine),
                  "engine");
    EXPECT_STREQ(EvolveLogger::category_channel_suffix(
                     EvolveLogCategory::Fitness),
                  "fitness");
    EXPECT_STREQ(EvolveLogger::category_channel_suffix(
                     EvolveLogCategory::Substitute),
                  "substitute");
    EXPECT_STREQ(EvolveLogger::category_channel_suffix(
                     EvolveLogCategory::DAG),
                  "dag");
}

// ---------------------------------------------------------------------------
// Direct log() / detail() — channel emission when channels attached
// ---------------------------------------------------------------------------

TEST(EvolveLoggerAbsorb, LogPublishesToCategorySubchannel) {
    EvolveLogger logger;
    auto svc = make_default_channel_service();
    logger.set_channels(svc.get());
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.evolution.engine";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    logger.start(EvolveLogLevel::Logical,
                  static_cast<uint32_t>(EvolveLogCategory::All));
    logger.log(EvolveLogCategory::Engine, "hello-from-engine");
    logger.stop();
    svc->flush_for_tests();

    ASSERT_GE(cap->size(), 1u);
    auto msgs = cap->captured();
    EXPECT_EQ(msgs.front().channel, "etil.evolution.engine");
    EXPECT_EQ(msgs.front().tags.at("level"), "logical");
}

TEST(EvolveLoggerAbsorb, DetailOnlyAtGranularLevel) {
    EvolveLogger logger;
    auto svc = make_default_channel_service();
    logger.set_channels(svc.get());
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.evolution.fitness";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    logger.start(EvolveLogLevel::Logical,
                  static_cast<uint32_t>(EvolveLogCategory::All));
    logger.detail(EvolveLogCategory::Fitness, "not-emitted-at-logical");
    svc->flush_for_tests();
    EXPECT_EQ(cap->size(), 0u);

    logger.start(EvolveLogLevel::Granular,
                  static_cast<uint32_t>(EvolveLogCategory::All));
    logger.detail(EvolveLogCategory::Fitness, "emitted-at-granular");
    logger.stop();
    svc->flush_for_tests();
    ASSERT_GE(cap->size(), 1u);
    EXPECT_EQ(cap->captured().front().tags.at("level"), "granular");
}

TEST(EvolveLoggerAbsorb, OffLevelNoEmission) {
    EvolveLogger logger;
    auto svc = make_default_channel_service();
    logger.set_channels(svc.get());
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.evolution.**";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    // No start() call — level stays Off.
    logger.log(EvolveLogCategory::Engine, "should-not-appear");
    EXPECT_EQ(cap->size(), 0u);
}

TEST(EvolveLoggerAbsorb, FileAndChannelBothReceiveLines) {
    EvolveLogger logger;
    auto tmp = std::filesystem::temp_directory_path() /
               ("etil_evolve_absorb_" + std::to_string(::getpid()));
    std::filesystem::create_directories(tmp);
    logger.set_directory(tmp.string());

    auto svc = make_default_channel_service();
    logger.set_channels(svc.get());
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.evolution.engine";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    logger.start(EvolveLogLevel::Logical,
                  static_cast<uint32_t>(EvolveLogCategory::All));
    logger.log(EvolveLogCategory::Engine, "dual-write");
    logger.stop();
    svc->flush_for_tests();

    // Channel saw it.
    ASSERT_GE(cap->size(), 1u);

    // File saw it.
    bool found_in_file = false;
    for (auto& entry : std::filesystem::directory_iterator(tmp)) {
        if (entry.path().extension() != ".log") continue;
        std::ifstream in(entry.path());
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("dual-write") != std::string::npos) {
                found_in_file = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found_in_file);
    std::filesystem::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// EvolutionEngine::set_channels forwards into the embedded logger
// ---------------------------------------------------------------------------

TEST(EvolveLoggerAbsorb, EngineForwardsChannelsIntoLogger) {
    Dictionary dict;
    register_primitives(dict);
    std::ostringstream out;
    Interpreter interp{dict, out};
    interp.interpret_line(": square dup * ;");

    EvolutionConfig cfg;
    cfg.generation_size = 2;
    EvolutionEngine engine(cfg, dict);

    auto svc = make_default_channel_service();
    engine.set_channels(svc.get());
    EXPECT_EQ(engine.logger().channels(), svc.get());

    // Run a short evolution with the engine logger active; expect at
    // least one etil.evolution.engine message.
    engine.logger().start(EvolveLogLevel::Logical,
                           static_cast<uint32_t>(EvolveLogCategory::All));
    auto cap = make_test_capture_sink();
    RouteSpec spec;
    spec.channel_pattern = "etil.evolution.engine";
    spec.sink = cap;
    svc->add_route(std::move(spec));

    std::vector<TestCase> tests;
    tests.push_back({{Value(int64_t(4))}, {Value(int64_t(16))}});
    engine.register_tests("square", std::move(tests));
    engine.evolve_word("square");
    engine.logger().stop();
    svc->flush_for_tests();

    EXPECT_GE(cap->size(), 1u);
}
