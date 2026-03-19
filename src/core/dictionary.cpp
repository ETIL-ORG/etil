// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/dictionary.hpp"
#include "etil/selection/selection_engine.hpp"
#include <spdlog/spdlog.h>

namespace etil::core {

std::atomic<uint64_t> Dictionary::next_id_{1};

uint64_t Dictionary::next_id() {
    return next_id_.fetch_add(1, std::memory_order_relaxed);
}

void Dictionary::register_handler_word(const std::string& word) {
    absl::MutexLock lock(&mutex_);
    auto& wc = concepts_[word];
    if (wc.name.empty()) {
        wc.name = word;
    }
    // No implementation added — concept exists for metadata attachment.
    // If the word already exists (e.g., `words` is both a primitive and
    // a handler), this is a no-op.
}

void Dictionary::register_word(const std::string& word, WordImplPtr impl) {
    absl::MutexLock lock(&mutex_);
    auto& wc = concepts_[word];
    if (wc.name.empty()) {
        wc.name = word;
    }
    wc.implementations.push_back(std::move(impl));
    spdlog::debug("Registered implementation for concept '{}'", word);
}

std::optional<WordImplPtr>
Dictionary::lookup(const std::string& word) const {
    absl::ReaderMutexLock lock(&mutex_);
    auto it = concepts_.find(word);
    if (it == concepts_.end() || it->second.implementations.empty()) {
        return std::nullopt;
    }
    return it->second.implementations.back();
}

std::optional<WordImplPtr>
Dictionary::select(const std::string& word,
                   etil::selection::SelectionEngine& engine) const {
    absl::ReaderMutexLock lock(&mutex_);
    auto it = concepts_.find(word);
    if (it == concepts_.end() || it->second.implementations.empty()) {
        return std::nullopt;
    }
    auto* selected = engine.select(it->second.implementations);
    if (!selected) return std::nullopt;
    // Find the WordImplPtr that owns this impl and return a copy (which addrefs)
    for (auto& impl_ptr : it->second.implementations) {
        if (impl_ptr.get() == selected) {
            return impl_ptr;  // copy addrefs via WordImplPtr copy ctor
        }
    }
    return std::nullopt;
}

std::optional<std::vector<WordImplPtr>>
Dictionary::get_implementations(const std::string& word) const {
    absl::ReaderMutexLock lock(&mutex_);
    auto it = concepts_.find(word);
    if (it == concepts_.end()) {
        return std::nullopt;
    }
    return it->second.implementations;
}

bool Dictionary::forget_word(const std::string& word) {
    absl::MutexLock lock(&mutex_);
    auto it = concepts_.find(word);
    if (it == concepts_.end() || it->second.implementations.empty()) {
        return false;
    }
    it->second.implementations.pop_back();
    if (it->second.implementations.empty()) {
        concepts_.erase(it);
    }
    generation_.fetch_add(1, std::memory_order_release);
    return true;
}

bool Dictionary::forget_all(const std::string& word) {
    absl::MutexLock lock(&mutex_);
    auto it = concepts_.find(word);
    if (it == concepts_.end()) {
        return false;
    }
    concepts_.erase(it);
    generation_.fetch_add(1, std::memory_order_release);
    return true;
}

std::vector<std::string> Dictionary::word_names() const {
    absl::ReaderMutexLock lock(&mutex_);
    std::vector<std::string> names;
    names.reserve(concepts_.size());
    for (const auto& [name, _] : concepts_) {
        names.push_back(name);
    }
    return names;
}

size_t Dictionary::concept_count() const {
    absl::ReaderMutexLock lock(&mutex_);
    return concepts_.size();
}

bool Dictionary::set_concept_metadata(const std::string& word,
                                      const std::string& key,
                                      MetadataFormat format,
                                      std::string content) {
    absl::MutexLock lock(&mutex_);
    auto it = concepts_.find(word);
    if (it == concepts_.end()) {
        return false;
    }
    it->second.metadata.set(key, format, std::move(content));
    return true;
}

std::optional<MetadataEntry> Dictionary::get_concept_metadata(
    const std::string& word, const std::string& key) const {
    absl::ReaderMutexLock lock(&mutex_);
    auto it = concepts_.find(word);
    if (it == concepts_.end()) {
        return std::nullopt;
    }
    return it->second.metadata.get(key);
}

bool Dictionary::remove_concept_metadata(const std::string& word,
                                         const std::string& key) {
    absl::MutexLock lock(&mutex_);
    auto it = concepts_.find(word);
    if (it == concepts_.end()) {
        return false;
    }
    return it->second.metadata.remove(key);
}

std::vector<std::string> Dictionary::concept_metadata_keys(
    const std::string& word) const {
    absl::ReaderMutexLock lock(&mutex_);
    auto it = concepts_.find(word);
    if (it == concepts_.end()) {
        return {};
    }
    return it->second.metadata.keys();
}

} // namespace etil::core
