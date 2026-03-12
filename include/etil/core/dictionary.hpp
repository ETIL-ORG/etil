#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/word_impl.hpp"
#include "etil/core/metadata.hpp"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace etil::core {

struct WordConcept {
    std::string name;
    std::vector<WordImplPtr> implementations;
    MetadataMap metadata;
};

/// Thread-safe dictionary mapping concept names to word implementations.
class Dictionary {
public:
    Dictionary() = default;

    /// Register an implementation under a concept name.
    void register_word(const std::string& word, WordImplPtr impl);

    /// Register a handler word (concept with no implementations).
    /// Creates a WordConcept entry so metadata can be attached via
    /// set_concept_metadata(). If the word already exists, this is a no-op.
    void register_handler_word(const std::string& word);

    /// Look up the latest (most recently registered) implementation for a concept.
    /// Returns nullopt if not found.
    std::optional<WordImplPtr> lookup(const std::string& word) const;

    /// Get all implementations for a concept.
    /// Returns nullopt if concept not found.
    std::optional<std::vector<WordImplPtr>>
    get_implementations(const std::string& word) const;

    /// Get all concept names.
    std::vector<std::string> word_names() const;

    /// Remove the latest implementation of a word. If it was the last
    /// implementation, the entire concept is erased. Returns true if found.
    bool forget_word(const std::string& word);

    /// Remove a word and all its implementations from the dictionary.
    /// Returns true if found and removed.
    bool forget_all(const std::string& word);

    /// Number of distinct concepts registered.
    size_t concept_count() const;

    /// Set concept-level metadata. Returns false if concept not found.
    bool set_concept_metadata(const std::string& word, const std::string& key,
                              MetadataFormat format, std::string content);

    /// Get concept-level metadata. Returns nullopt if concept or key not found.
    std::optional<MetadataEntry> get_concept_metadata(
        const std::string& word, const std::string& key) const;

    /// Remove concept-level metadata. Returns false if concept or key not found.
    bool remove_concept_metadata(const std::string& word,
                                 const std::string& key);

    /// List all metadata keys on a concept. Returns empty if concept not found.
    std::vector<std::string> concept_metadata_keys(
        const std::string& word) const;

    /// Generate a unique WordImpl ID.
    static uint64_t next_id();

    /// Monotonically increasing generation counter.  Incremented on every
    /// forget_word() / forget_all() so that cached WordImpl* pointers in
    /// compiled bytecode can detect staleness cheaply.
    uint64_t generation() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }

private:
    mutable absl::Mutex mutex_;
    absl::flat_hash_map<std::string, WordConcept> concepts_ ABSL_GUARDED_BY(mutex_);
    static std::atomic<uint64_t> next_id_;
    std::atomic<uint64_t> generation_{0};
};

} // namespace etil::core
