#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// IClock — abstract monotonic clock source for Manifold.
///
/// Introduced in Phase 5a to give tests a deterministic time source
/// for producer-registry last_published_ns assertions (Phase 5a.5)
/// and for future rate-limiter / scheduling tests. The production
/// implementation (SystemClock) returns `steady_clock::now()` in
/// nanoseconds; the test implementation (ManualClock) returns
/// whatever the test last `set_time_ns()` wrote.
///
/// Every DefaultChannelService instance owns a std::shared_ptr<IClock>;
/// tests inject a ManualClock via the constructor overload.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

namespace etil::manifold {

class IClock {
public:
    virtual ~IClock() = default;

    /// Monotonic time in nanoseconds since some fixed epoch.
    /// The epoch is implementation-defined — callers should only
    /// compute deltas, never treat the absolute value as meaningful.
    virtual uint64_t now_ns() const = 0;
};

/// Production clock: wraps std::chrono::steady_clock.
class SystemClock : public IClock {
public:
    uint64_t now_ns() const override {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }
};

std::shared_ptr<IClock> make_system_clock();

/// Test clock: returns whatever the test last wrote via set_time_ns().
/// All reads/writes are atomic so the clock can be safely used across
/// threads (publisher writes "now", dispatcher reads "then", assertion
/// checks the stamped value).
class ManualClock : public IClock {
public:
    explicit ManualClock(uint64_t initial_ns = 0) : now_(initial_ns) {}

    void set_time_ns(uint64_t t) {
        now_.store(t, std::memory_order_release);
    }

    /// Advance the clock by `delta_ns` and return the new value.
    uint64_t advance_ns(uint64_t delta_ns) {
        return now_.fetch_add(delta_ns, std::memory_order_acq_rel) + delta_ns;
    }

    uint64_t now_ns() const override {
        return now_.load(std::memory_order_acquire);
    }

private:
    std::atomic<uint64_t> now_;
};

std::shared_ptr<ManualClock> make_manual_clock(uint64_t initial_ns = 0);

} // namespace etil::manifold
