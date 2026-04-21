// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/clock.hpp"

namespace etil::manifold {

std::shared_ptr<IClock> make_system_clock() {
    return std::make_shared<SystemClock>();
}

std::shared_ptr<ManualClock> make_manual_clock(uint64_t initial_ns) {
    return std::make_shared<ManualClock>(initial_ns);
}

} // namespace etil::manifold
