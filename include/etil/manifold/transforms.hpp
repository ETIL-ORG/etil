#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Built-in Manifold transform factories. All transforms are pure and
/// thread-safe; stateful transforms (rate_limiter, sampler) guard their
/// state internally.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "etil/manifold/transform.hpp"

namespace etil::manifold {

/// Drop messages whose tags["level"] is below `min_level`. Unknown or
/// absent level passes through. Levels: trace < debug < info < warn <
/// error < critical.
std::shared_ptr<ITransform> make_level_filter(std::string min_level);

/// Drop messages whose channel does not match the given pattern.
/// Useful when composing fan-outs that share a route.
std::shared_ptr<ITransform> make_channel_filter(std::string pattern);

/// Drop messages missing the given tag key/value pair.
std::shared_ptr<ITransform> make_tag_filter(std::string key, std::string value);

/// Set a tag on every message passing through. Overwrites existing
/// values under the same key.
std::shared_ptr<ITransform> make_tag_annotator(std::string key, std::string value);

/// Convert the payload to a string using a default stringification
/// (std::string pass-through; channel + tags fallback for non-string
/// payloads). Replaces the payload with the generated string and sets
/// payload_type to std::string.
std::shared_ptr<ITransform> make_formatter();

/// Token-bucket rate limiter. At most max_per_second messages pass
/// through; excess drops. Simple synchronous refill. Drop counter
/// exposed via the transform's dropped_count(); use in combination
/// with a periodic summary publisher for drop visibility.
std::shared_ptr<ITransform> make_rate_limiter(double max_per_second);

/// Replicate each input message onto the given additional channels.
/// The original message retains its channel; one new message per
/// target channel is emitted with the same payload and tags.
/// route_trace is inherited; each emitted message has the new channel
/// appended to its trace by the router.
std::shared_ptr<ITransform> make_fan_out(std::vector<std::string> extra_channels);

/// Keep one message in N (deterministic, not random). Counter starts
/// at 0; messages with (count % N == 0) pass, others drop.
std::shared_ptr<ITransform> make_sampler(uint64_t one_in_n);

/// Encode the message to JSON (channel, tags, payload, origin) and
/// replace the payload with the JSON string. Used before UDP/TCP
/// sinks in Phase 3.
std::shared_ptr<ITransform> make_json_encoder();

} // namespace etil::manifold
