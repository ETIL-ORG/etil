#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <optional>
#include <ostream>
#include <sstream>
#include <string>

namespace etil::core {

/// Read from stream until unescaped delimiter, processing escape sequences.
///
/// Strips one leading space (same convention as ." / s").
/// Supported escapes: \n \r \t \0 \a \b \f \\ \| \%hh (hex byte).
/// Returns the processed string, or std::nullopt on error (writes to err).
std::optional<std::string> read_escaped_string(std::istringstream& iss,
                                                char delimiter,
                                                std::ostream& err);

} // namespace etil::core
