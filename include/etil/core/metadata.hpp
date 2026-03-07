#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "absl/container/flat_hash_map.h"

#include <optional>
#include <string>
#include <vector>

namespace etil::core {

/// Format of metadata content.
enum class MetadataFormat { Text, Markdown, Html, Code, Json, Jsonl };

/// A single metadata entry with key, format, and content.
struct MetadataEntry {
    std::string key;
    MetadataFormat format;
    std::string content;
};

/// O(1) key-value store for metadata entries.
///
/// Not thread-safe by itself; relies on external synchronization
/// (Dictionary's absl::Mutex for concept-level, construction-time
/// or thread-local access for WordImpl).
class MetadataMap {
public:
    /// Set (or overwrite) a metadata entry.
    void set(const std::string& key, MetadataFormat format, std::string content);

    /// Get a metadata entry by key. Returns nullopt if not found.
    std::optional<MetadataEntry> get(const std::string& key) const;

    /// Remove a metadata entry. Returns true if found and removed.
    bool remove(const std::string& key);

    /// List all keys.
    std::vector<std::string> keys() const;

    /// Check if a key exists.
    bool has(const std::string& key) const;

    /// Number of entries.
    size_t size() const;

    /// True if no entries.
    bool empty() const;

    /// Direct access to the underlying map.
    const absl::flat_hash_map<std::string, MetadataEntry>& entries() const;

private:
    absl::flat_hash_map<std::string, MetadataEntry> entries_;
};

} // namespace etil::core
