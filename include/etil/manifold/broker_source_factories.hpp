#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Factory functions for concrete broker sources. Each returns
/// nullptr when its transport is not compiled in (ETIL_BUILD_NATS_SINK
/// / ETIL_BUILD_AMQP_SINK — reusing the sink-side flags since the
/// same client library provides both directions).

#include <memory>

#include "etil/manifold/broker_source.hpp"

namespace etil::manifold {

std::shared_ptr<BrokerSourceBase> make_nats_source(
    BrokerSourceConfig cfg,
    std::weak_ptr<ChannelService> channels);
bool nats_source_compiled_in();

std::shared_ptr<BrokerSourceBase> make_amqp_source(
    BrokerSourceConfig cfg,
    std::weak_ptr<ChannelService> channels);
bool amqp_source_compiled_in();

} // namespace etil::manifold
