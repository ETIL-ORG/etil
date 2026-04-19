// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/codec_resolver.hpp"

#include "etil/manifold/transforms.hpp"

namespace etil::manifold {

std::shared_ptr<ITransform> resolve_codec(std::string_view codec) {
    if (codec.empty() || codec == "json")    return make_json_encoder();
    if (codec == "msgpack")                   return make_msgpack_encoder();
    if (codec == "cbor")                      return make_cbor_encoder();
    if (codec == "raw")                       return make_raw_passthrough();
    return nullptr;
}

} // namespace etil::manifold
