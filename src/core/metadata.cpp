// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/metadata.hpp"

namespace etil::core {

void MetadataMap::set(const std::string& key, MetadataFormat format,
                      std::string content) {
    entries_[key] = MetadataEntry{key, format, std::move(content)};
}

std::optional<MetadataEntry> MetadataMap::get(const std::string& key) const {
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool MetadataMap::remove(const std::string& key) {
    return entries_.erase(key) > 0;
}

std::vector<std::string> MetadataMap::keys() const {
    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const auto& [k, _] : entries_) {
        result.push_back(k);
    }
    return result;
}

bool MetadataMap::has(const std::string& key) const {
    return entries_.contains(key);
}

size_t MetadataMap::size() const {
    return entries_.size();
}

bool MetadataMap::empty() const {
    return entries_.empty();
}

const absl::flat_hash_map<std::string, MetadataEntry>&
MetadataMap::entries() const {
    return entries_;
}

} // namespace etil::core
