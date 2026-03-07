// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/word_impl.hpp"

namespace etil::core {

bool TypeSignature::matches(const TypeSignature& other) const {
    // TODO: Implement type matching logic
    // For now, exact match required
    return inputs == other.inputs && outputs == other.outputs;
}

} // namespace etil::core
