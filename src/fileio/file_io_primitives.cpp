// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Sync file I/O primitives removed in AVO Phase 4.
// Use the async words (read-file, write-file, etc.) or
// observable streaming words (obs-read-lines, obs-write-file, etc.).

#include "etil/fileio/file_io_primitives.hpp"
#include "etil/core/dictionary.hpp"

namespace etil::fileio {

void register_file_io_primitives(etil::core::Dictionary& /*dict*/) {
    // No sync primitives registered — removed in v0.9.7 (AVO Phase 4)
}

} // namespace etil::fileio
