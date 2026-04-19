#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// NATS broker sink (Phase 3b). Concrete subclass of BrokerSinkBase
/// using nats.c. Compiled only when ETIL_NATS_SINK_ENABLED is
/// defined (ETIL_BUILD_NATS_SINK=ON in CMake); otherwise
/// make_nats_sink() returns nullptr and the TIL word
/// `channel-tap-nats` surfaces a clear error to the caller.
///
/// Design: doc B §16.4 + §16.5 + 20260419A Phase 3b.

#include <memory>
#include <string>

#include "etil/manifold/broker_sink.hpp"

namespace etil::manifold {

class ChannelService;

/// Construct a NATS broker sink bound to the given ChannelService.
/// Connects eagerly to the configured URL; returns nullptr on
/// connect failure (logs to etil.logging.error). Returns nullptr
/// unconditionally when NATS support is not compiled in.
std::shared_ptr<BrokerSinkBase> make_nats_sink(
    BrokerSinkConfig cfg,
    std::weak_ptr<ChannelService> channels);

/// True when the binary was built with ETIL_BUILD_NATS_SINK=ON.
bool nats_sink_compiled_in();

} // namespace etil::manifold
