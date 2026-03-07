#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/metadata.hpp"
#include "etil/core/word_impl.hpp"
#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace etil::core {

/// Convert a MetadataFormat to its string name.
std::string format_to_string(MetadataFormat format);

/// Parse a string to MetadataFormat. Returns nullopt on failure.
std::optional<MetadataFormat> parse_metadata_format(const std::string& s);

/// Serialize a MetadataEntry to JSON.
nlohmann::json to_json(const MetadataEntry& entry);

/// Deserialize a MetadataEntry from JSON.
MetadataEntry metadata_entry_from_json(const nlohmann::json& j);

/// Serialize a MetadataMap to JSON (object of key -> entry).
nlohmann::json to_json(const MetadataMap& map);

/// Deserialize a MetadataMap from JSON.
MetadataMap metadata_map_from_json(const nlohmann::json& j);

/// Serialize a WordImpl to JSON (name, id, generation, weight, signature, metadata).
nlohmann::json word_impl_to_json(const WordImpl& impl);

/// Serialize a word concept to JSON (name, implementations, concept metadata).
nlohmann::json word_concept_to_json(const std::string& name,
                                    const std::vector<WordImplPtr>& impls,
                                    const MetadataMap& concept_meta);

} // namespace etil::core
