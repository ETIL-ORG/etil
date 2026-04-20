#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// RouteState — per-route runtime counters + RouteSpec bundle.
///
/// Internal to the Manifold implementation (not exposed in
/// include/etil/manifold/). Moved out of default_service.cpp's
/// anonymous namespace in Phase 5a.3 so both default_service.cpp
/// and dispatcher.cpp can work with DeliveryItem{shared_ptr<RouteState>,
/// Message}.

#include <atomic>
#include <cstdint>

#include "etil/manifold/route_spec.hpp"

namespace etil::manifold {

struct RouteState {
    RouteHandle handle;
    RouteSpec   spec;
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> dropped{0};
};

} // namespace etil::manifold
