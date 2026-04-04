#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/word_impl.hpp"
#include "etil/core/dictionary.hpp"

#include <map>
#include <string>
#include <vector>

namespace etil::evolution {

/// Index of word signatures for fast lookup of type-compatible words.
class SignatureIndex {
public:
    using T = etil::core::TypeSignature::Type;

    /// Rebuild index from dictionary. Call when dictionary changes.
    void rebuild(const etil::core::Dictionary& dict);

    /// Find all words with a given stack depth effect (consumed, produced).
    std::vector<std::string> find_compatible(int consumed, int produced) const;

    /// Find all words with exact type signature match.
    std::vector<std::string> find_exact(
        const etil::core::TypeSignature& sig) const;

    /// Find words matching (consumed, produced) whose input types are compatible
    /// with the given stack types (using the promotion-aware compatibility matrix).
    /// Falls back to find_compatible() if stack_types is empty or all Unknown.
    std::vector<std::string> find_type_compatible(
        int consumed, int produced,
        const std::vector<T>& stack_types) const;

    /// Find words with same stack effect AND matching semantic tags.
    /// Returns pairs of (word_name, match_level):
    ///   1 = same tags, 2 = overlapping tags, 3 = signature only
    std::vector<std::pair<std::string, int>> find_tiered(
        int consumed, int produced,
        const std::vector<std::string>& tags) const;

    /// Find words restricted to a given pool (empty pool = full dictionary).
    std::vector<std::string> find_restricted(
        int consumed, int produced,
        const std::vector<std::string>& pool) const;

    /// Get semantic tags for a word (cached from dictionary metadata).
    std::vector<std::string> get_tags(const std::string& word) const;

    /// Can a value of stack_type be passed to a word expecting word_input_type?
    /// Encodes widening promotions (Integer→Float) but not narrowing (Float→Integer).
    static bool type_compatible(T stack_type, T word_input_type);

    /// Dictionary generation when index was last built.
    uint64_t generation() const { return generation_; }

private:
    std::map<std::pair<int,int>, std::vector<std::string>> by_effect_;
    std::map<std::string, std::vector<T>> input_types_;  // word -> input types
    std::map<std::string, std::vector<std::string>> tags_;  // word -> tags
    uint64_t generation_ = 0;
};

} // namespace etil::evolution
