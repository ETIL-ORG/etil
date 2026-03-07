// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/metadata_json.hpp"
#include <nlohmann/json.hpp>

namespace etil::core {

std::string format_to_string(MetadataFormat format) {
    switch (format) {
        case MetadataFormat::Text:     return "text";
        case MetadataFormat::Markdown: return "markdown";
        case MetadataFormat::Html:     return "html";
        case MetadataFormat::Code:     return "code";
        case MetadataFormat::Json:     return "json";
        case MetadataFormat::Jsonl:    return "jsonl";
    }
    return "text";
}

std::optional<MetadataFormat> parse_metadata_format(const std::string& s) {
    if (s == "text")     return MetadataFormat::Text;
    if (s == "markdown") return MetadataFormat::Markdown;
    if (s == "html")     return MetadataFormat::Html;
    if (s == "code")     return MetadataFormat::Code;
    if (s == "json")     return MetadataFormat::Json;
    if (s == "jsonl")    return MetadataFormat::Jsonl;
    return std::nullopt;
}

nlohmann::json to_json(const MetadataEntry& entry) {
    return nlohmann::json{
        {"key", entry.key},
        {"format", format_to_string(entry.format)},
        {"content", entry.content}
    };
}

MetadataEntry metadata_entry_from_json(const nlohmann::json& j) {
    MetadataEntry entry;
    entry.key = j.at("key").get<std::string>();
    auto fmt = parse_metadata_format(j.at("format").get<std::string>());
    entry.format = fmt.value_or(MetadataFormat::Text);
    entry.content = j.at("content").get<std::string>();
    return entry;
}

nlohmann::json to_json(const MetadataMap& map) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [key, entry] : map.entries()) {
        j[key] = to_json(entry);
    }
    return j;
}

MetadataMap metadata_map_from_json(const nlohmann::json& j) {
    MetadataMap map;
    for (auto it = j.begin(); it != j.end(); ++it) {
        auto entry = metadata_entry_from_json(it.value());
        map.set(entry.key, entry.format, std::move(entry.content));
    }
    return map;
}

nlohmann::json word_impl_to_json(const WordImpl& impl) {
    // Serialize signature inputs/outputs
    auto type_to_string = [](TypeSignature::Type t) -> std::string {
        switch (t) {
            case TypeSignature::Type::Unknown: return "unknown";
            case TypeSignature::Type::Integer: return "integer";
            case TypeSignature::Type::Float:   return "float";
            case TypeSignature::Type::String:  return "string";
            case TypeSignature::Type::Array:   return "array";
            case TypeSignature::Type::Custom:  return "custom";
        }
        return "unknown";
    };

    nlohmann::json sig = nlohmann::json::object();
    nlohmann::json inputs = nlohmann::json::array();
    for (auto t : impl.signature().inputs) {
        inputs.push_back(type_to_string(t));
    }
    nlohmann::json outputs = nlohmann::json::array();
    for (auto t : impl.signature().outputs) {
        outputs.push_back(type_to_string(t));
    }
    sig["inputs"] = inputs;
    sig["outputs"] = outputs;

    return nlohmann::json{
        {"name", impl.name()},
        {"id", impl.id()},
        {"generation", impl.generation()},
        {"weight", impl.weight()},
        {"signature", sig},
        {"metadata", to_json(impl.metadata())}
    };
}

nlohmann::json word_concept_to_json(const std::string& name,
                                    const std::vector<WordImplPtr>& impls,
                                    const MetadataMap& concept_meta) {
    nlohmann::json impl_array = nlohmann::json::array();
    for (const auto& impl : impls) {
        if (impl) {
            impl_array.push_back(word_impl_to_json(*impl));
        }
    }
    return nlohmann::json{
        {"name", name},
        {"implementations", impl_array},
        {"metadata", to_json(concept_meta)}
    };
}

} // namespace etil::core
