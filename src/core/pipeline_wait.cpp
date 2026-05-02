// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/pipeline_wait.hpp"

#include <atomic>

namespace etil::core {

namespace {
// Backing store: integer microseconds, default 30 seconds.
// Stored as int64_t (atomic-trivial across platforms) rather than
// std::atomic<double> (whose lock-freedom varies by libstdc++ version).
constexpr int64_t DEFAULT_TIMEOUT_US = 30 * 1'000'000;
std::atomic<int64_t> g_pipeline_wait_timeout_us{DEFAULT_TIMEOUT_US};
} // anonymous namespace

void set_pipeline_wait_timeout_seconds(double seconds) {
    int64_t us = (seconds <= 0.0)
        ? 0
        : static_cast<int64_t>(seconds * 1'000'000.0);
    g_pipeline_wait_timeout_us.store(us, std::memory_order_relaxed);
}

double get_pipeline_wait_timeout_seconds() {
    return static_cast<double>(g_pipeline_wait_timeout_us.load(std::memory_order_relaxed))
           / 1'000'000.0;
}

std::chrono::steady_clock::time_point compute_pipeline_deadline() {
    int64_t us = g_pipeline_wait_timeout_us.load(std::memory_order_relaxed);
    if (us <= 0) return std::chrono::steady_clock::time_point::max();
    return std::chrono::steady_clock::now() + std::chrono::microseconds(us);
}

} // namespace etil::core
