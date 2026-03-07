#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


namespace etil::core { class Dictionary; }

namespace etil::fileio {

/// Register synchronous file I/O primitives into the dictionary.
void register_file_io_primitives(etil::core::Dictionary& dict);

} // namespace etil::fileio
