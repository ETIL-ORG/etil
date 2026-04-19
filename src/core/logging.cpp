// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/logging.hpp"

#include <cstdlib>
#include <filesystem>
#include <mutex>

#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace etil::core::logging {

namespace {

constexpr size_t kRotateSize = 10 * 1024 * 1024;  // 10 MB
constexpr size_t kRotateFiles = 5;
constexpr char kPattern[] = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v";

struct State {
    absl::Mutex mu;
    bool initialized ABSL_GUARDED_BY(mu) = false;
    spdlog::level::level_enum default_level ABSL_GUARDED_BY(mu) =
        spdlog::level::info;

    // Shared sinks — every logger attaches both.
    std::shared_ptr<spdlog::sinks::sink> file_sink ABSL_GUARDED_BY(mu);
    std::shared_ptr<spdlog::sinks::sink> stderr_sink ABSL_GUARDED_BY(mu);

    // Logger cache keyed by name.
    absl::flat_hash_map<std::string, std::shared_ptr<spdlog::logger>>
        loggers ABSL_GUARDED_BY(mu);
};

State& state() {
    static State s;
    return s;
}

spdlog::level::level_enum env_override(spdlog::level::level_enum fallback) {
    const char* env = std::getenv("ETIL_LOG_LEVEL");
    if (env == nullptr || env[0] == '\0') return fallback;
    return level_from_string(env);
}

std::shared_ptr<spdlog::logger> build_logger_locked(
    State& s, const std::string& name) ABSL_EXCLUSIVE_LOCKS_REQUIRED(s.mu) {
    std::vector<spdlog::sink_ptr> sinks;
    if (s.file_sink) sinks.push_back(s.file_sink);
    if (s.stderr_sink) sinks.push_back(s.stderr_sink);

    auto logger = std::make_shared<spdlog::logger>(
        name, sinks.begin(), sinks.end());
    logger->set_level(s.default_level);
    logger->set_pattern(kPattern);
    logger->flush_on(spdlog::level::warn);
    return logger;
}

}  // namespace

void init(const std::string& log_dir,
          spdlog::level::level_enum default_level) {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    if (s.initialized) return;

    s.default_level = env_override(default_level);

    // stderr sink is always configured. On WASM, Emscripten routes stderr to
    // console.error; on native it shows to the operator. Level is WARN+ so
    // INFO and below are only sent to the file sink (if configured).
    auto stderr_sink =
        std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    stderr_sink->set_level(spdlog::level::warn);
    s.stderr_sink = stderr_sink;

#ifndef ETIL_WASM_BUILD
    // Native builds get a rotating file sink if log_dir is usable.
    if (!log_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(log_dir, ec);
        if (!ec) {
            std::filesystem::path file = log_dir;
            file /= "etil.log";
            try {
                auto file_sink =
                    std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        file.string(), kRotateSize, kRotateFiles);
                file_sink->set_level(spdlog::level::trace);
                s.file_sink = file_sink;
            } catch (const spdlog::spdlog_ex&) {
                // File sink creation failed — the stderr sink remains.
                // We intentionally do not log here (logger isn't ready).
                s.file_sink.reset();
            }
        }
    }
#else
    (void)log_dir;  // WASM: no file sink.
#endif

    s.initialized = true;
}

void shutdown() {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    for (auto& [name, logger] : s.loggers) {
        if (logger) logger->flush();
    }
    s.loggers.clear();
    s.file_sink.reset();
    s.stderr_sink.reset();
    s.initialized = false;
}

std::shared_ptr<spdlog::logger> get(const std::string& name) {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    auto it = s.loggers.find(name);
    if (it != s.loggers.end()) return it->second;

    // Safety net: if the caller never ran init(), synthesize a stderr-only
    // sink set on-demand. This supports bootstrap-path callers that run
    // before init() (rare) and tests that skip init entirely.
    if (!s.stderr_sink) {
        auto stderr_sink =
            std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        stderr_sink->set_level(spdlog::level::warn);
        s.stderr_sink = stderr_sink;
    }

    auto logger = build_logger_locked(s, name);
    s.loggers.emplace(name, logger);
    return logger;
}

void set_level(const std::string& name, spdlog::level::level_enum level) {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    auto it = s.loggers.find(name);
    if (it != s.loggers.end()) {
        it->second->set_level(level);
        return;
    }
    // Logger doesn't exist yet — create it and set the level.
    auto logger = build_logger_locked(s, name);
    logger->set_level(level);
    s.loggers.emplace(name, logger);
}

void set_all_levels(spdlog::level::level_enum level) {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    s.default_level = level;
    for (auto& [name, logger] : s.loggers) {
        if (logger) logger->set_level(level);
    }
}

spdlog::level::level_enum level_from_string(const std::string& s) {
    if (s == "trace") return spdlog::level::trace;
    if (s == "debug") return spdlog::level::debug;
    if (s == "info") return spdlog::level::info;
    if (s == "warn" || s == "warning") return spdlog::level::warn;
    if (s == "error" || s == "err") return spdlog::level::err;
    if (s == "critical" || s == "crit") return spdlog::level::critical;
    if (s == "off") return spdlog::level::off;
    return spdlog::level::info;
}

bool is_initialized() {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    return s.initialized;
}

}  // namespace etil::core::logging
