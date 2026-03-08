#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


namespace etil::core { class Dictionary; }

namespace etil::net {

/// Register HTTP client primitives in the dictionary.
///
/// Words registered:
///   http-get ( url headers-map -- bytes status-code flag )
///     Perform an HTTP/HTTPS GET request with extra headers.
///     headers-map is a HeapMap of string key→string value pairs.
///     Returns response body as opaque bytes (HeapByteArray), HTTP
///     status code, and success flag.  On error, pushes only flag=false.
void register_http_primitives(etil::core::Dictionary& dict);

} // namespace etil::net
