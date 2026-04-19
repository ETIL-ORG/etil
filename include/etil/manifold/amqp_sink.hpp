#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// AMQP 1.0 broker sink (Phase 3c). Concrete subclass of
/// BrokerSinkBase using Qpid Proton-C++. Compiled only when
/// ETIL_AMQP_SINK_ENABLED is defined (ETIL_BUILD_AMQP_SINK=ON in
/// CMake); otherwise make_amqp_sink() returns nullptr and the TIL
/// word `channel-tap-amqp` surfaces a clear error to the caller.
///
/// Address translation per doc B §16.3: dotted channel names map to
/// AMQP addresses 1:1, with `**` → `#` (greedy multi-segment
/// wildcard) at subscribe-time on the broker. The sink itself
/// publishes to the literal channel name — translation happens on
/// the broker side when consumers subscribe with wildcards.
///
/// Design: doc B §16.3 + §16.5 + 20260419A Phase 3c.

#include <memory>
#include <string>

#include "etil/manifold/broker_sink.hpp"

namespace etil::manifold {

class ChannelService;

/// Construct an AMQP 1.0 broker sink bound to the given
/// ChannelService. Starts a background Proton container thread and
/// opens a sender to the configured URL; returns nullptr on
/// immediate-failure paths (logs to etil.logging.error). Returns
/// nullptr unconditionally when AMQP support is not compiled in.
std::shared_ptr<BrokerSinkBase> make_amqp_sink(
    BrokerSinkConfig cfg,
    std::weak_ptr<ChannelService> channels);

/// True when the binary was built with ETIL_BUILD_AMQP_SINK=ON.
bool amqp_sink_compiled_in();

} // namespace etil::manifold
