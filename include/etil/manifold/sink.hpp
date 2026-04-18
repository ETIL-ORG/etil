#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Sink interface for Manifold. A sink is anything that consumes messages
/// from a channel — spdlog backend, file, observable, test capture, etc.

#include "etil/manifold/message.hpp"

namespace etil::manifold {

class ISink {
public:
    virtual ~ISink() = default;

    /// Consume a single message. Must be thread-safe if the sink is
    /// attached to a route that may be invoked from multiple threads.
    virtual void accept(const Message& msg) = 0;

    /// Flush any pending buffered writes. Optional; default is no-op.
    virtual void flush() {}
};

} // namespace etil::manifold
