// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/signature_index.hpp"

namespace etil::evolution {

using namespace etil::core;

void SignatureIndex::rebuild(const Dictionary& dict) {
    by_effect_.clear();
    generation_ = dict.generation();

    for (const auto& name : dict.word_names()) {
        auto impl = dict.lookup(name);
        if (!impl) continue;
        const auto& sig = (*impl)->signature();
        if (sig.variable_inputs || sig.variable_outputs) continue;
        int consumed = static_cast<int>(sig.inputs.size());
        int produced = static_cast<int>(sig.outputs.size());
        by_effect_[{consumed, produced}].push_back(name);
    }
}

std::vector<std::string> SignatureIndex::find_compatible(
    int consumed, int produced) const {
    auto it = by_effect_.find({consumed, produced});
    if (it == by_effect_.end()) return {};
    return it->second;
}

std::vector<std::string> SignatureIndex::find_exact(
    const TypeSignature& sig) const {
    // For now, just match by depth (consumed/produced count)
    // Type-level matching is a future enhancement
    return find_compatible(
        static_cast<int>(sig.inputs.size()),
        static_cast<int>(sig.outputs.size()));
}

} // namespace etil::evolution
