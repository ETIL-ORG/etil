// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/mcp_server.hpp"
#include "etil/mcp/session.hpp"
#include "etil/core/metadata_json.hpp"

namespace etil::mcp {

void McpServer::register_all_resources() {
    // 1. etil://dictionary
    register_resource(
        "etil://dictionary",
        "Dictionary",
        "All words with descriptions and implementation counts",
        "application/json",
        [this](const std::string& uri) { return resource_dictionary(uri); }
    );

    // 2. etil://word/{name}
    register_resource(
        "etil://word/{name}",
        "Word Details",
        "Full word details including implementations and metadata",
        "application/json",
        [this](const std::string& uri) { return resource_word(uri); }
    );

    // 3. etil://stack
    register_resource(
        "etil://stack",
        "Stack",
        "Current data stack snapshot",
        "application/json",
        [this](const std::string& uri) { return resource_stack(uri); }
    );

    // 4. etil://session/stats
    register_resource(
        "etil://session/stats",
        "Session Stats",
        "Per-session CPU time, memory, and interpreter metrics",
        "application/json",
        [this](const std::string& uri) { return resource_session_stats(uri); }
    );
}

// ---------------------------------------------------------------------------
// Resource implementations
// ---------------------------------------------------------------------------

nlohmann::json McpServer::resource_dictionary(const std::string& uri) {
    auto& session = *current_session_;
    auto names = session.dict->word_names();
    std::sort(names.begin(), names.end());

    nlohmann::json words = nlohmann::json::array();
    for (const auto& name : names) {
        nlohmann::json entry = {{"name", name}};

        auto desc = session.dict->get_concept_metadata(name, "description");
        if (desc) entry["description"] = desc->content;

        auto impls = session.dict->get_implementations(name);
        if (impls) entry["implCount"] = impls->size();

        words.push_back(entry);
    }

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"uri", uri},
        {"mimeType", "application/json"},
        {"text", nlohmann::json({{"words", words}}).dump()}
    });

    return {{"contents", content_array}};
}

nlohmann::json McpServer::resource_word(const std::string& uri) {
    auto& session = *current_session_;
    // Extract word name from URI: "etil://word/{name}"
    const std::string prefix = "etil://word/";
    std::string name = uri.substr(prefix.size());

    auto impls = session.dict->get_implementations(name);
    if (!impls) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"uri", uri},
            {"mimeType", "text/plain"},
            {"text", "Word not found: " + name}
        });
        return {{"contents", content_array}};
    }

    // Build concept metadata
    auto meta_keys = session.dict->concept_metadata_keys(name);
    etil::core::MetadataMap concept_meta;
    for (const auto& key : meta_keys) {
        auto entry = session.dict->get_concept_metadata(name, key);
        if (entry) {
            concept_meta.set(key, entry->format, std::string(entry->content));
        }
    }

    nlohmann::json word_json = etil::core::word_concept_to_json(name, *impls, concept_meta);

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"uri", uri},
        {"mimeType", "application/json"},
        {"text", word_json.dump()}
    });

    return {{"contents", content_array}};
}

nlohmann::json McpServer::resource_stack(const std::string& uri) {
    auto& session = *current_session_;
    const auto& ds = session.interp->context().data_stack();

    nlohmann::json elements = nlohmann::json::array();
    for (size_t i = 0; i < ds.size(); ++i) {
        elements.push_back(etil::core::Interpreter::format_value(ds[i]));
    }

    nlohmann::json stack_data = {
        {"depth", ds.size()},
        {"elements", elements},
        {"status", session.interp->stack_status()}
    };

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"uri", uri},
        {"mimeType", "application/json"},
        {"text", stack_data.dump()}
    });

    return {{"contents", content_array}};
}

nlohmann::json McpServer::resource_session_stats(const std::string& uri) {
    auto& session = *current_session_;

    // Refresh dynamic counters
    session.stats.current_rss_bytes = SessionStats::read_rss_bytes();
    if (session.stats.current_rss_bytes > session.stats.peak_rss_bytes) {
        session.stats.peak_rss_bytes = session.stats.current_rss_bytes;
    }

    auto stats_json = session.stats.to_json(
        session.dict->concept_count(),
        session.interp->context().data_stack().size());

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"uri", uri},
        {"mimeType", "application/json"},
        {"text", stats_json.dump()}
    });

    return {{"contents", content_array}};
}

} // namespace etil::mcp
