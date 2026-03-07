#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


namespace etil::core {

class Dictionary;

/// Register string primitives (s+, s=, slength, type, etc.)
void register_string_primitives(Dictionary& dict);

/// Register array primitives (array-new, array-push, array-get, etc.)
void register_array_primitives(Dictionary& dict);

/// Register byte array primitives (bytes-new, bytes-get, bytes-set, etc.)
void register_byte_primitives(Dictionary& dict);

/// Register map primitives (map-new, map-set, map-get, etc.)
void register_map_primitives(Dictionary& dict);

} // namespace etil::core
