// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/signature_index.hpp"

#include <algorithm>
#include <sstream>

namespace etil::evolution {

using namespace etil::core;

void SignatureIndex::rebuild(const Dictionary& dict) {
    by_effect_.clear();
    input_types_.clear();
    tags_.clear();
    generation_ = dict.generation();

    for (const auto& name : dict.word_names()) {
        auto impl = dict.lookup(name);
        if (!impl) continue;
        const auto& sig = (*impl)->signature();
        if (sig.variable_inputs || sig.variable_outputs) continue;
        int consumed = static_cast<int>(sig.inputs.size());
        int produced = static_cast<int>(sig.outputs.size());
        by_effect_[{consumed, produced}].push_back(name);

        // Cache input types for type-compatible filtering
        input_types_[name] = sig.inputs;

        // Cache semantic tags: manual first, fall back to inferred
        auto tag_meta = dict.get_concept_metadata(name, "semantic-tags");
        if (!tag_meta) {
            tag_meta = dict.get_concept_metadata(name, "semantic-tags-inferred");
        }
        if (tag_meta) {
            std::vector<std::string> word_tags;
            std::istringstream iss(tag_meta->content);
            std::string tag;
            while (iss >> tag) word_tags.push_back(tag);
            if (!word_tags.empty()) tags_[name] = std::move(word_tags);
        }
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

std::vector<std::string> SignatureIndex::find_restricted(
    int consumed, int produced,
    const std::vector<std::string>& pool) const {
    if (pool.empty()) return find_compatible(consumed, produced);

    auto all = find_compatible(consumed, produced);
    std::vector<std::string> filtered;
    for (const auto& name : all) {
        for (const auto& p : pool) {
            if (name == p) { filtered.push_back(name); break; }
        }
    }
    return filtered;
}

std::vector<std::string> SignatureIndex::get_tags(const std::string& word) const {
    auto it = tags_.find(word);
    if (it == tags_.end()) return {};
    return it->second;
}

std::vector<std::pair<std::string, int>> SignatureIndex::find_tiered(
    int consumed, int produced,
    const std::vector<std::string>& tags) const {
    auto compatible = find_compatible(consumed, produced);
    std::vector<std::pair<std::string, int>> results;

    for (const auto& name : compatible) {
        auto word_tags = get_tags(name);
        if (tags.empty() || word_tags.empty()) {
            results.push_back({name, 3});  // signature only
            continue;
        }
        // Check tag overlap
        size_t matches = 0;
        for (const auto& t : tags) {
            for (const auto& wt : word_tags) {
                if (t == wt) { matches++; break; }
            }
        }
        if (matches == tags.size() && matches == word_tags.size()) {
            results.push_back({name, 1});  // exact tag match
        } else if (matches > 0) {
            results.push_back({name, 2});  // overlapping
        } else {
            results.push_back({name, 3});  // signature only
        }
    }

    // Sort by level (best first)
    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    return results;
}

bool SignatureIndex::type_compatible(T stack_type, T word_input_type) {
    // Unknown on either side: always compatible (permissive)
    if (stack_type == T::Unknown || word_input_type == T::Unknown)
        return true;
    // Exact match
    if (stack_type == word_input_type)
        return true;
    // Integer→Float: compatible (widening promotion, auto-promoted by arith_binary)
    if (stack_type == T::Integer && word_input_type == T::Float)
        return true;
    // All other pairs: not compatible (Float→Integer is narrowing, Boolean→Integer
    // is undefined, different concrete types need explicit bridges)
    return false;
}

std::vector<std::string> SignatureIndex::find_type_compatible(
    int consumed, int produced,
    const std::vector<T>& stack_types) const {

    auto depth_matches = find_compatible(consumed, produced);

    // Fall back to depth-only if stack_types is empty
    if (stack_types.empty())
        return depth_matches;

    // Fall back to depth-only if all stack types are Unknown
    bool all_unknown = true;
    for (const auto& t : stack_types) {
        if (t != T::Unknown) { all_unknown = false; break; }
    }
    if (all_unknown)
        return depth_matches;

    // Filter by type compatibility
    std::vector<std::string> filtered;
    for (const auto& name : depth_matches) {
        auto it = input_types_.find(name);
        if (it == input_types_.end()) {
            // No cached input types — include as permissive fallback
            filtered.push_back(name);
            continue;
        }
        const auto& word_inputs = it->second;
        bool compatible = true;
        size_t check_count = std::min(stack_types.size(), word_inputs.size());
        for (size_t i = 0; i < check_count; ++i) {
            if (!type_compatible(stack_types[i], word_inputs[i])) {
                compatible = false;
                break;
            }
        }
        if (compatible) filtered.push_back(name);
    }

    // Never return empty — fall back to depth-only if no type-compatible match
    if (filtered.empty())
        return depth_matches;

    return filtered;
}

} // namespace etil::evolution
