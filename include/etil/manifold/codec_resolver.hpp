#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Codec resolver — maps broker-tap codec-name strings to the matching
/// encoder transform (doc B broker-payload-codec resolution). Used by
/// the Phase 3b/3c broker sinks to install the correct serializer on
/// the RouteSpec transform chain at tap-word install time.
///
/// Codec names:
///   "json"    — JSON text via nlohmann::json (existing make_json_encoder).
///   "msgpack" — MessagePack binary via nlohmann::json::to_msgpack.
///   "cbor"    — CBOR binary via nlohmann::json::to_cbor.
///   "raw"     — pass-through; requires payload_type == HeapByteArray-ish.
///
/// Unknown codec names return nullptr — callers must surface this as a
/// route-install failure (TOS = false in the TIL layer).

#include <memory>
#include <string_view>

#include "etil/manifold/transform.hpp"

namespace etil::manifold {

/// Return the encoder transform for the named codec, or nullptr if
/// the name is not one of the four recognized codecs.
/// Empty string is treated as an alias for "json".
std::shared_ptr<ITransform> resolve_codec(std::string_view codec);

} // namespace etil::manifold
