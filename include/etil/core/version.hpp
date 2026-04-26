#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

// Version + build-timestamp strings are *defined* in src/core/version.cpp,
// which is generated per-configure from src/core/version.cpp.in. Keeping
// the strings out of this header means a version bump or fresh build
// timestamp invalidates only one TU instead of every TU that includes
// this header — preserves ccache hit rate across version bumps.

namespace etil::core {

extern const char* ETIL_VERSION;
extern const char* ETIL_BUILD_TIMESTAMP;

} // namespace etil::core
