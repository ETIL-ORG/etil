#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Dedicated debug logging for the evolution pipeline.
///
/// Two levels:
///   Logical  — human-readable narrative (what happened and why)
///   Granular — function-level tracing (parameters and return values)
///
/// 14-category bitmask enables/disables subsystems independently.
/// Logs to dedicated timestamped files: YYYYMMDDThhmmss-evolve.log

#include <cstdint>
#include <fstream>
#include <string>

namespace etil::evolution {

/// Category bitmask for selective evolution logging.
enum class EvolveLogCategory : uint32_t {
    None        = 0,
    Engine      = 1 << 0,   // Generation loop, child creation, population management
    Decompile   = 1 << 1,   // Bytecode → AST conversion
    Substitute  = 1 << 2,   // Word substitution mutation
    Perturb     = 1 << 3,   // Constant perturbation mutation
    Move        = 1 << 4,   // Block move mutation
    ControlFlow = 1 << 5,   // Control flow wrap/unwrap mutation
    Grow        = 1 << 6,   // Grow mutation (future)
    Shrink      = 1 << 7,   // Shrink mutation (future)
    Crossover   = 1 << 8,   // AST crossover between parents
    Repair      = 1 << 9,   // Type repair (shuffle insertion)
    Compile     = 1 << 10,  // AST → bytecode compilation
    Fitness     = 1 << 11,  // Test case evaluation
    Selection   = 1 << 12,  // Parent selection, weight update, pruning
    Bridge      = 1 << 13,  // Bridge word insertion (future)
    All         = 0xFFFFFFFF,
};

inline constexpr uint32_t operator|(EvolveLogCategory a, EvolveLogCategory b) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}

/// Log verbosity level.
enum class EvolveLogLevel {
    Off      = 0,  // No logging (zero overhead)
    Logical  = 1,  // Human-readable narrative
    Granular = 2,  // Function-level tracing with parameters
};

/// Dedicated file-based logger for the evolution pipeline.
///
/// Logs are NOT mixed with spdlog, interpreter output, or MCP streams.
/// Each start() opens a new file: YYYYMMDDThhmmss-evolve.log
///
/// When level == Off, enabled()/granular() are false — zero overhead.
class EvolveLogger {
public:
    EvolveLogger() = default;
    ~EvolveLogger();

    // Lifecycle
    void start(EvolveLogLevel level, uint32_t category_mask);
    void stop();
    void set_directory(const std::string& dir);

    /// True if logging is active for this category at any level.
    bool enabled(EvolveLogCategory cat) const {
        return level_ != EvolveLogLevel::Off &&
               (categories_ & static_cast<uint32_t>(cat)) != 0;
    }

    /// True if granular (detail) logging is active for this category.
    bool granular(EvolveLogCategory cat) const {
        return level_ == EvolveLogLevel::Granular &&
               (categories_ & static_cast<uint32_t>(cat)) != 0;
    }

    /// Emit a logical-level log line. Timestamp and category tag prepended.
    void log(EvolveLogCategory cat, const std::string& msg);

    /// Emit a granular-level detail line. Only written when level == Granular.
    void detail(EvolveLogCategory cat, const std::string& msg);

    /// Current level (for TIL introspection).
    EvolveLogLevel level() const { return level_; }

    /// Current category mask.
    uint32_t categories() const { return categories_; }

    /// Whether the log file is currently open.
    bool is_open() const { return file_.is_open(); }

private:
    EvolveLogLevel level_ = EvolveLogLevel::Off;
    uint32_t categories_ = static_cast<uint32_t>(EvolveLogCategory::All);
    std::string directory_;
    std::ofstream file_;

    std::string timestamp() const;
    std::string make_filename() const;
    static const char* category_tag(EvolveLogCategory cat);
};

} // namespace etil::evolution
