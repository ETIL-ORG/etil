#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Transform interface for Manifold. A transform is a function
/// Message → vector<Message>: 0 outputs drops the message, 1 is the
/// common case, >1 implements fan-out. Transforms compose left-to-right
/// in RouteSpec::transforms.

#include <vector>

#include "etil/manifold/message.hpp"

namespace etil::manifold {

class ITransform {
public:
    virtual ~ITransform() = default;

    /// Apply the transform. May produce 0..N output messages. Must be
    /// thread-safe — transforms may be invoked concurrently on
    /// different producer threads on the same route.
    virtual std::vector<Message> apply(Message msg) = 0;
};

} // namespace etil::manifold
