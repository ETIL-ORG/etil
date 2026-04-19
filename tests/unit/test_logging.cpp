// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/logging.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

// Temporary directory helper for tests that need a writable log_dir.
struct TempLogDir {
    std::filesystem::path path;

    TempLogDir() {
        auto base = std::filesystem::temp_directory_path();
        path = base / ("etil_logging_test_" + std::to_string(::getpid()) + "_" +
                       std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(path);
    }

    ~TempLogDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    std::string string() const { return path.string(); }
};

}  // namespace

// ---------------------------------------------------------------------------
// Factory and caching
// ---------------------------------------------------------------------------

TEST(LoggingTest, GetReturnsCachedInstance) {
    etil::core::logging::shutdown();  // clean slate
    auto a = etil::core::logging::get("test.cache");
    auto b = etil::core::logging::get("test.cache");
    EXPECT_EQ(a.get(), b.get());
}

TEST(LoggingTest, DifferentNamesReturnDifferentLoggers) {
    etil::core::logging::shutdown();
    auto a = etil::core::logging::get("test.name.a");
    auto b = etil::core::logging::get("test.name.b");
    EXPECT_NE(a.get(), b.get());
}

TEST(LoggingTest, GetSafetyNetWhenNotInitialized) {
    etil::core::logging::shutdown();
    // Should not throw or return nullptr even without init().
    auto logger = etil::core::logging::get("test.safety_net");
    ASSERT_NE(logger, nullptr);
    // Should be able to log without crashing.
    logger->info("safety net emit");
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST(LoggingTest, InitCreatesFileSink) {
    etil::core::logging::shutdown();
    TempLogDir dir;
    etil::core::logging::init(dir.string(), spdlog::level::info);
    ASSERT_TRUE(etil::core::logging::is_initialized());

    auto log = etil::core::logging::get("test.file_sink");
    log->info("written to file");
    log->flush();

    std::filesystem::path expected = dir.path / "etil.log";
    EXPECT_TRUE(std::filesystem::exists(expected))
        << "Expected log file at " << expected.string();

    etil::core::logging::shutdown();
}

TEST(LoggingTest, InitIsIdempotent) {
    etil::core::logging::shutdown();
    TempLogDir dir;
    etil::core::logging::init(dir.string(), spdlog::level::info);
    EXPECT_TRUE(etil::core::logging::is_initialized());
    // Second init should be a no-op and not throw.
    etil::core::logging::init(dir.string(), spdlog::level::debug);
    EXPECT_TRUE(etil::core::logging::is_initialized());
    etil::core::logging::shutdown();
}

TEST(LoggingTest, InitFallsBackOnUnusableLogDir) {
    etil::core::logging::shutdown();
    // A path that definitely can't be created or written.
    etil::core::logging::init("/root/nonexistent/etil_test_dir",
                              spdlog::level::info);
    // Should still be initialized — file sink just skipped.
    EXPECT_TRUE(etil::core::logging::is_initialized());
    // Logger should still work (stderr sink).
    auto log = etil::core::logging::get("test.fallback");
    ASSERT_NE(log, nullptr);
    log->warn("fallback path emit");
    etil::core::logging::shutdown();
}

// ---------------------------------------------------------------------------
// Levels
// ---------------------------------------------------------------------------

TEST(LoggingTest, SetLevelAppliesToExistingLogger) {
    etil::core::logging::shutdown();
    auto log = etil::core::logging::get("test.level.a");
    log->set_level(spdlog::level::info);
    etil::core::logging::set_level("test.level.a", spdlog::level::err);
    EXPECT_EQ(log->level(), spdlog::level::err);
}

TEST(LoggingTest, SetLevelCreatesLoggerWhenAbsent) {
    etil::core::logging::shutdown();
    etil::core::logging::set_level("test.level.new", spdlog::level::trace);
    auto log = etil::core::logging::get("test.level.new");
    EXPECT_EQ(log->level(), spdlog::level::trace);
}

TEST(LoggingTest, SetAllLevelsAffectsAllExistingLoggers) {
    etil::core::logging::shutdown();
    auto a = etil::core::logging::get("test.all.a");
    auto b = etil::core::logging::get("test.all.b");
    etil::core::logging::set_all_levels(spdlog::level::critical);
    EXPECT_EQ(a->level(), spdlog::level::critical);
    EXPECT_EQ(b->level(), spdlog::level::critical);
}

TEST(LoggingTest, LevelFromStringParsesKnownNames) {
    using etil::core::logging::level_from_string;
    EXPECT_EQ(level_from_string("trace"), spdlog::level::trace);
    EXPECT_EQ(level_from_string("debug"), spdlog::level::debug);
    EXPECT_EQ(level_from_string("info"), spdlog::level::info);
    EXPECT_EQ(level_from_string("warn"), spdlog::level::warn);
    EXPECT_EQ(level_from_string("warning"), spdlog::level::warn);
    EXPECT_EQ(level_from_string("error"), spdlog::level::err);
    EXPECT_EQ(level_from_string("err"), spdlog::level::err);
    EXPECT_EQ(level_from_string("critical"), spdlog::level::critical);
    EXPECT_EQ(level_from_string("crit"), spdlog::level::critical);
    EXPECT_EQ(level_from_string("off"), spdlog::level::off);
}

TEST(LoggingTest, LevelFromStringDefaultsToInfoOnUnknown) {
    using etil::core::logging::level_from_string;
    EXPECT_EQ(level_from_string("bogus"), spdlog::level::info);
    EXPECT_EQ(level_from_string(""), spdlog::level::info);
}

// ---------------------------------------------------------------------------
// Thread safety — concurrent get() calls must not race.
// ---------------------------------------------------------------------------

TEST(LoggingTest, ConcurrentGetReturnsSameInstance) {
    etil::core::logging::shutdown();
    constexpr int kThreads = 16;
    std::vector<std::shared_ptr<spdlog::logger>> loggers(kThreads);
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([i, &loggers] {
            loggers[i] = etil::core::logging::get("test.concurrent");
        });
    }
    for (auto& t : threads) t.join();
    auto* first = loggers[0].get();
    ASSERT_NE(first, nullptr);
    for (int i = 1; i < kThreads; ++i) {
        EXPECT_EQ(loggers[i].get(), first);
    }
}

// ---------------------------------------------------------------------------
// is_initialized reflects init/shutdown cycle.
// ---------------------------------------------------------------------------

TEST(LoggingTest, IsInitializedReflectsLifecycle) {
    etil::core::logging::shutdown();
    EXPECT_FALSE(etil::core::logging::is_initialized());
    TempLogDir dir;
    etil::core::logging::init(dir.string(), spdlog::level::info);
    EXPECT_TRUE(etil::core::logging::is_initialized());
    etil::core::logging::shutdown();
    EXPECT_FALSE(etil::core::logging::is_initialized());
}
