// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/mcp_server.hpp"
#include "etil/mcp/session.hpp"
#include "etil/core/metadata_json.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_matrix.hpp"
#include "etil/core/heap_observable.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/lvfs/lvfs.hpp"

#ifdef ETIL_HTTP_CLIENT_ENABLED
#include "etil/net/http_client_config.hpp"
#include "etil/net/http_primitives.hpp"
#endif

#ifdef ETIL_JWT_ENABLED
#include "etil/mcp/auth_config.hpp"
#endif

#ifdef ETIL_MONGODB_ENABLED
#include "etil/db/mongo_client.hpp"
#include "etil/db/mongo_primitives.hpp"
#include "etil/aaa/audit_log.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace etil::mcp {

// ---------------------------------------------------------------------------
// MCP execution limits — applied to tool_interpret() only.
// REPL and unit tests use unlimited defaults (UINT64_MAX / SIZE_MAX).
// ---------------------------------------------------------------------------
constexpr uint64_t MCP_INSTRUCTION_BUDGET  = 10'000'000;    // 10M instructions
constexpr double   MCP_TIMEOUT_SECONDS     = 30.0;          // wall-clock deadline
constexpr size_t   MCP_MAX_STACK_DEPTH     = 100'000;       // items
constexpr size_t   MCP_MAX_CALL_DEPTH      = 1'000;         // recursion levels
constexpr size_t   MCP_MAX_CODE_SIZE       = 1'048'576;     // 1 MB input
constexpr size_t   MCP_MAX_DATA_SIZE       = 10'485'760;    // 10 MB file data
constexpr size_t   MCP_MAX_CONCEPTS        = 10'000;        // dictionary concept limit
// MCP_MAX_OUTPUT_SIZE is defined in mcp_server.hpp

// ---------------------------------------------------------------------------
// SessionStats implementation
// ---------------------------------------------------------------------------

void SessionStats::reset() {
    interpret_call_count = 0;
    interpret_wall_ns = 0;
    interpret_cpu_ns = 0;
    current_rss_bytes = 0;
    peak_rss_bytes = 0;
    session_start_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

nlohmann::json SessionStats::to_json(uint64_t concept_count,
                                      size_t stack_depth) const {
    auto now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    return {
        {"interpretCallCount", interpret_call_count},
        {"interpretWallMs", interpret_wall_ns / 1'000'000},
        {"interpretCpuMs", interpret_cpu_ns / 1'000'000},
        {"currentRssBytes", current_rss_bytes},
        {"currentRssMb",
         static_cast<double>(current_rss_bytes) / (1024.0 * 1024.0)},
        {"peakRssBytes", peak_rss_bytes},
        {"peakRssMb",
         static_cast<double>(peak_rss_bytes) / (1024.0 * 1024.0)},
        {"dictionaryConceptCount", concept_count},
        {"dataStackDepth", stack_depth},
        {"sessionStartMs", session_start_ms},
        {"sessionUptimeMs", now_ms - session_start_ms},
    };
}

uint64_t SessionStats::read_rss_bytes() {
    std::ifstream statm("/proc/self/statm");
    if (!statm.is_open()) return 0;
    uint64_t size_pages = 0, rss_pages = 0;
    statm >> size_pages >> rss_pages;
    if (statm.fail()) return 0;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;
    return rss_pages * static_cast<uint64_t>(page_size);
}

uint64_t SessionStats::cpu_time_ns() {
    struct timespec ts{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

void McpServer::register_all_tools() {
    // 1. interpret
    register_tool(
        "interpret",
        "Execute TIL code and return output, errors, and stack state",
        {
            {"type", "object"},
            {"properties", {
                {"code", {{"type", "string"}, {"description", "TIL code to execute"}}}
            }},
            {"required", nlohmann::json::array({"code"})}
        },
        [this](const nlohmann::json& params) { return tool_interpret(params); }
    );

    // 2. list_words
    register_tool(
        "list_words",
        "List all dictionary words with optional category filter",
        {
            {"type", "object"},
            {"properties", {
                {"category", {{"type", "string"},
                    {"description", "Filter by category (from help metadata)"}}}
            }}
        },
        [this](const nlohmann::json& params) { return tool_list_words(params); }
    );

    // 3. get_word_info
    register_tool(
        "get_word_info",
        "Get full introspection data for a word: metadata, implementations, profile, signature",
        {
            {"type", "object"},
            {"properties", {
                {"name", {{"type", "string"}, {"description", "Word name to inspect"}}}
            }},
            {"required", nlohmann::json::array({"name"})}
        },
        [this](const nlohmann::json& params) { return tool_get_word_info(params); }
    );

    // 4. get_stack
    register_tool(
        "get_stack",
        "Get the current data stack as structured data",
        {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        },
        [this](const nlohmann::json& params) { return tool_get_stack(params); }
    );

    // 5. set_weight
    register_tool(
        "set_weight",
        "Set the selection weight/probability for a word implementation",
        {
            {"type", "object"},
            {"properties", {
                {"word", {{"type", "string"}, {"description", "Word name"}}},
                {"weight", {{"type", "number"}, {"description", "New weight value"}}},
                {"impl_index", {{"type", "integer"},
                    {"description", "Implementation index (default: latest)"}}}
            }},
            {"required", nlohmann::json::array({"word", "weight"})}
        },
        [this](const nlohmann::json& params) { return tool_set_weight(params); }
    );

    // 6. reset
    register_tool(
        "reset",
        "Clear interpreter state and reload startup files",
        {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        },
        [this](const nlohmann::json& params) { return tool_reset(params); }
    );

    // 7. get_session_stats
    register_tool(
        "get_session_stats",
        "Get per-session CPU time, memory, and interpreter metrics",
        {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        },
        [this](const nlohmann::json& params) {
            return tool_get_session_stats(params);
        }
    );

    // 8. write_file
    register_tool(
        "write_file",
        "Write a file to the session home directory (used by TUI /load)",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"},
                    {"description", "Relative path within session home directory"}}},
                {"content", {{"type", "string"},
                    {"description", "File content to write"}}}
            }},
            {"required", nlohmann::json::array({"path", "content"})}
        },
        [this](const nlohmann::json& params) { return tool_write_file(params); }
    );

    // 9. list_files
    register_tool(
        "list_files",
        "List files in the session home directory",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"},
                    {"description", "Relative path within session home (default: root)"}}}
            }}
        },
        [this](const nlohmann::json& params) { return tool_list_files(params); }
    );

    // 10. read_file
    register_tool(
        "read_file",
        "Read a file from the session home directory",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"},
                    {"description", "Relative path within session home directory"}}}
            }},
            {"required", nlohmann::json::array({"path"})}
        },
        [this](const nlohmann::json& params) { return tool_read_file(params); }
    );

    // 11. list_sessions
    register_tool(
        "list_sessions",
        "List all active sessions (admin only)",
        {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        },
        [this](const nlohmann::json& params) {
            return tool_list_sessions(params);
        }
    );

    // 12. kick_session
    register_tool(
        "kick_session",
        "Terminate a session by ID (admin only)",
        {
            {"type", "object"},
            {"properties", {
                {"session_id", {
                    {"type", "string"},
                    {"description", "ID of the session to terminate"}}}
            }},
            {"required", nlohmann::json::array({"session_id"})}
        },
        [this](const nlohmann::json& params) {
            return tool_kick_session(params);
        }
    );

    // 13. manage_allowlist (gated below)

    // 14–21. Admin tools (role/user management)
#ifdef ETIL_JWT_ENABLED
    register_tool(
        "admin_list_roles",
        "List all defined roles and the default role",
        {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        },
        [this](const nlohmann::json& params) {
            return tool_admin_list_roles(params);
        }
    );

    register_tool(
        "admin_get_role",
        "Get full permissions for a role",
        {
            {"type", "object"},
            {"properties", {
                {"role", {{"type", "string"},
                    {"description", "Role name to inspect"}}}
            }},
            {"required", nlohmann::json::array({"role"})}
        },
        [this](const nlohmann::json& params) {
            return tool_admin_get_role(params);
        }
    );

    register_tool(
        "admin_set_role",
        "Create or update a role with the given permissions",
        {
            {"type", "object"},
            {"properties", {
                {"role", {{"type", "string"},
                    {"description", "Role name to create or update"}}},
                {"permissions", {{"type", "object"},
                    {"description", "Permissions object (same keys as roles.json)"}}}
            }},
            {"required", nlohmann::json::array({"role", "permissions"})}
        },
        [this](const nlohmann::json& params) {
            return tool_admin_set_role(params);
        }
    );

    register_tool(
        "admin_delete_role",
        "Delete a role (fails if users are still assigned to it)",
        {
            {"type", "object"},
            {"properties", {
                {"role", {{"type", "string"},
                    {"description", "Role name to delete"}}}
            }},
            {"required", nlohmann::json::array({"role"})}
        },
        [this](const nlohmann::json& params) {
            return tool_admin_delete_role(params);
        }
    );

    register_tool(
        "admin_list_users",
        "List all user-to-role mappings",
        {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        },
        [this](const nlohmann::json& params) {
            return tool_admin_list_users(params);
        }
    );

    register_tool(
        "admin_set_user_role",
        "Assign a role to a user",
        {
            {"type", "object"},
            {"properties", {
                {"user_id", {{"type", "string"},
                    {"description", "User ID (e.g. github:12345)"}}},
                {"role", {{"type", "string"},
                    {"description", "Role to assign"}}}
            }},
            {"required", nlohmann::json::array({"user_id", "role"})}
        },
        [this](const nlohmann::json& params) {
            return tool_admin_set_user_role(params);
        }
    );

    register_tool(
        "admin_delete_user",
        "Remove a user-to-role mapping (user will fall back to default role)",
        {
            {"type", "object"},
            {"properties", {
                {"user_id", {{"type", "string"},
                    {"description", "User ID to remove"}}}
            }},
            {"required", nlohmann::json::array({"user_id"})}
        },
        [this](const nlohmann::json& params) {
            return tool_admin_delete_user(params);
        }
    );

    register_tool(
        "admin_set_default_role",
        "Set the default role for new/unknown users",
        {
            {"type", "object"},
            {"properties", {
                {"role", {{"type", "string"},
                    {"description", "Role name to set as default (must exist)"}}}
            }},
            {"required", nlohmann::json::array({"role"})}
        },
        [this](const nlohmann::json& params) {
            return tool_admin_set_default_role(params);
        }
    );

    register_tool(
        "admin_reload_config",
        "Reload auth configuration from disk (roles.json, users.json)",
        {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        },
        [this](const nlohmann::json& params) {
            return tool_admin_reload_config(params);
        }
    );
#endif

#if defined(ETIL_HTTP_CLIENT_ENABLED) && defined(ETIL_JWT_ENABLED)
    register_tool(
        "manage_allowlist",
        "Add or remove HTTP domains from session allowlist (admin only)",
        {
            {"type", "object"},
            {"properties", {
                {"action", {
                    {"type", "string"},
                    {"enum", nlohmann::json::array({"add", "remove", "list"})},
                    {"description", "Action to perform on the allowlist"}}},
                {"domain", {
                    {"type", "string"},
                    {"description", "Domain to add or remove (not required for list)"}}}
            }},
            {"required", nlohmann::json::array({"action"})}
        },
        [this](const nlohmann::json& params) {
            return tool_manage_allowlist(params);
        }
    );
#endif
}

// ---------------------------------------------------------------------------
// Tool implementations
// ---------------------------------------------------------------------------

nlohmann::json McpServer::tool_interpret(const nlohmann::json& params) {
    auto& session = *current_session_;
    std::string code = params.at("code").get<std::string>();

    // Reject oversized input immediately
    if (code.size() > MCP_MAX_CODE_SIZE) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: code exceeds maximum size (" +
                     std::to_string(MCP_MAX_CODE_SIZE) + " bytes)"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Clear output buffers (reset releases heap memory back to OS)
    session.out_buf.reset();
    session.interp_out.clear();
    session.err_buf.reset();
    session.interp_err.clear();

    // Set execution limits before running
    auto& ctx = session.interp->context();
    uint64_t budget = MCP_INSTRUCTION_BUDGET;
    double timeout_seconds = MCP_TIMEOUT_SECONDS;

#ifdef ETIL_JWT_ENABLED
    // Override limits from role permissions if authenticated
    if (!session.role.empty() && auth_config_) {
        auto* perms = auth_config_->permissions_for(session.user_id);
        if (perms) {
            if (perms->instruction_budget > 0)
                budget = static_cast<uint64_t>(perms->instruction_budget);
            if (perms->interpret_execution_limit > 0)
                timeout_seconds =
                    static_cast<double>(perms->interpret_execution_limit);
            else if (perms->interpret_execution_limit == 0)
                timeout_seconds = 86400.0 * 365;  // effectively unlimited
        }
    }
#endif

    ctx.set_limits(budget, MCP_MAX_STACK_DEPTH,
                   MCP_MAX_CALL_DEPTH, timeout_seconds);

#ifdef ETIL_HTTP_CLIENT_ENABLED
    // Reset per-interpret HTTP fetch counter
    if (auto* hs = ctx.http_client_state()) {
        hs->reset_per_interpret();
    }
#endif

#ifdef ETIL_MONGODB_ENABLED
    // Reset per-interpret MongoDB query counter
    if (auto* ms = ctx.mongo_client_state()) {
        ms->reset_per_interpret();
    }
#endif

    // Wire real-time notification sender
    ctx.set_notification_sender([this](const std::string& msg) {
        emit_notification(msg);
    });

    // Wire targeted notification sender (for user-notification primitive)
    ctx.set_targeted_notification_sender(
        [this](const std::string& user_id, const std::string& msg) -> bool {
            return send_targeted_notification(user_id, msg);
        });

    // Execute with wall/CPU timing
    auto wall_start = std::chrono::steady_clock::now();
    auto cpu_start = SessionStats::cpu_time_ns();

    session.interp->interpret_line(code);

    auto wall_end = std::chrono::steady_clock::now();
    auto cpu_end = SessionStats::cpu_time_ns();

    // Extract abort state BEFORE reset_limits clears cancelled_
    bool aborted = ctx.abort_requested();
    bool abort_ok = ctx.abort_success();
    std::string abort_msg = ctx.abort_error_message();
    ctx.clear_abort();

    // Clear notification senders and restore unlimited defaults
    ctx.clear_notification_sender();
    ctx.clear_targeted_notification_sender();
    ctx.reset_limits();

    // Check dictionary concept count limit
    if (session.dict->concept_count() > MCP_MAX_CONCEPTS) {
        session.interp_err << "Error: dictionary concept limit exceeded ("
                           << session.dict->concept_count() << " > "
                           << MCP_MAX_CONCEPTS << ")\n";
    }

    // Update session stats
    session.stats.interpret_call_count++;
    session.stats.interpret_wall_ns += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            wall_end - wall_start)
            .count());
    session.stats.interpret_cpu_ns += (cpu_end - cpu_start);

    // Update RSS tracking
    session.stats.current_rss_bytes = SessionStats::read_rss_bytes();
    if (session.stats.current_rss_bytes > session.stats.peak_rss_bytes) {
        session.stats.peak_rss_bytes = session.stats.current_rss_bytes;
    }

#ifdef ETIL_JWT_ENABLED
    // Check cumulative session execution limit
    if (!session.role.empty() && auth_config_) {
        auto* perms = auth_config_->permissions_for(session.user_id);
        if (perms && perms->session_execution_limit > 0) {
            double cumulative_s =
                static_cast<double>(session.stats.interpret_wall_ns) / 1e9;
            double limit_s =
                static_cast<double>(perms->session_execution_limit);
            if (cumulative_s >= limit_s) {
                session.interp_err
                    << "Error: session execution limit exceeded ("
                    << static_cast<int>(cumulative_s) << "s used, "
                    << perms->session_execution_limit
                    << "s limit). Session terminated.\n";
                session.force_terminate = true;
            }
        }
    }
#endif

    // Extract output — take() moves the string out, leaving the
    // CappedStringBuf empty with no heap allocation.
    bool output_capped = session.out_buf.was_capped();
    std::string output = session.out_buf.take();
    std::string errors = session.err_buf.take();

    if (output_capped) {
        output += "\n... [output truncated at " +
                  std::to_string(MCP_MAX_OUTPUT_SIZE) + " bytes]";
    }

    // Abort with error: append abort message to errors
    if (aborted && !abort_ok) {
        if (!errors.empty() && errors.back() != '\n')
            errors += '\n';
        errors += abort_msg;
    }

    std::string stack_status = session.interp->stack_status();

    // Build the stack array (direct indexed access, bottom to top)
    nlohmann::json stack_array = nlohmann::json::array();
    const auto& ds = session.interp->context().data_stack();
    for (size_t i = 0; i < ds.size(); ++i) {
        stack_array.push_back(etil::core::Interpreter::format_value(ds[i]));
    }

    // Drain notification queue (already sent in real-time via
    // notifications/message JSON-RPC notifications; discard here).
    ctx.drain_notifications();

    nlohmann::json result_json = {
        {"output", output},
        {"errors", errors},
        {"stack", stack_array},
        {"stackStatus", stack_status}
    };

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", result_json.dump()}
    });

    bool is_error = !errors.empty() || (aborted && !abort_ok);
    return {
        {"content", content_array},
        {"isError", is_error}
    };
}

nlohmann::json McpServer::tool_list_words(const nlohmann::json& params) {
    auto& session = *current_session_;
    std::string category_filter;
    if (params.contains("category") && params["category"].is_string()) {
        category_filter = params["category"].get<std::string>();
    }

    auto words = session.interp->completable_words();

    nlohmann::json word_list = nlohmann::json::array();
    for (const auto& word : words) {
        // Get metadata if available
        auto desc_meta = session.dict->get_concept_metadata(word, "description");
        auto cat_meta = session.dict->get_concept_metadata(word, "category");
        auto effect_meta = session.dict->get_concept_metadata(word, "stack-effect");

        std::string description = desc_meta ? desc_meta->content : "";
        std::string category = cat_meta ? cat_meta->content : "";
        std::string stack_effect = effect_meta ? effect_meta->content : "";

        // Apply category filter
        if (!category_filter.empty() && category != category_filter) {
            continue;
        }

        nlohmann::json entry = {{"name", word}};
        if (!description.empty()) entry["description"] = description;
        if (!category.empty()) entry["category"] = category;
        if (!stack_effect.empty()) entry["stackEffect"] = stack_effect;

        // Get implementation count
        auto impls = session.dict->get_implementations(word);
        if (impls) {
            entry["implCount"] = impls->size();
        }

        word_list.push_back(entry);
    }

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({{"words", word_list}}).dump()}
    });

    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_get_word_info(const nlohmann::json& params) {
    auto& session = *current_session_;
    std::string name = params.at("name").get<std::string>();

    auto impls = session.dict->get_implementations(name);
    if (!impls) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Word not found: " + name}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Get concept metadata
    auto meta_keys = session.dict->concept_metadata_keys(name);
    etil::core::MetadataMap concept_meta;
    for (const auto& key : meta_keys) {
        auto entry = session.dict->get_concept_metadata(name, key);
        if (entry) {
            concept_meta.set(key, entry->format, std::string(entry->content));
        }
    }

    nlohmann::json word_json = etil::core::word_concept_to_json(name, *impls, concept_meta);

    // Add performance data for each implementation
    for (size_t i = 0; i < impls->size(); ++i) {
        const auto& impl = (*impls)[i];
        if (impl) {
            auto& profile = impl->profile();
            word_json["implementations"][i]["profile"] = {
                {"totalCalls", profile.total_calls.load(std::memory_order_relaxed)},
                {"meanDurationNs", profile.mean_duration_ns()},
                {"successRate", impl->success_rate()}
            };
        }
    }

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", word_json.dump()}
    });

    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_get_stack(const nlohmann::json& /*params*/) {
    auto& session = *current_session_;
    const auto& ds = session.interp->context().data_stack();
    size_t depth = ds.size();

    nlohmann::json elements = nlohmann::json::array();
    // Direct indexed access (bottom to top)
    for (size_t i = 0; i < depth; ++i) {
        const auto& val = ds[i];

        nlohmann::json elem = {
            {"value", etil::core::Interpreter::format_value(val)}
        };

        switch (val.type) {
            case etil::core::Value::Type::Integer:
                elem["type"] = "integer";
                elem["raw"] = val.as_int;
                break;
            case etil::core::Value::Type::Float:
                elem["type"] = "float";
                elem["raw"] = val.as_float;
                break;
            case etil::core::Value::Type::Boolean:
                elem["type"] = "boolean";
                elem["raw"] = val.as_bool();
                break;
            case etil::core::Value::Type::String:
                elem["type"] = "string";
                break;
            case etil::core::Value::Type::DataRef:
                elem["type"] = "dataref";
                elem["registry_index"] = val.dataref_index();
                elem["slot_offset"] = val.dataref_offset();
                break;
            case etil::core::Value::Type::Array:
                elem["type"] = "array";
                break;
            case etil::core::Value::Type::ByteArray:
                elem["type"] = "bytearray";
                break;
            case etil::core::Value::Type::Map:
                elem["type"] = "map";
                break;
            case etil::core::Value::Type::Json:
                elem["type"] = "json";
                if (val.as_ptr) {
                    elem["raw"] = val.as_json()->json();
                }
                break;
            case etil::core::Value::Type::Matrix:
                elem["type"] = "matrix";
                if (val.as_ptr) {
                    auto* mat = val.as_matrix();
                    elem["rows"] = mat->rows();
                    elem["cols"] = mat->cols();
                }
                break;
            case etil::core::Value::Type::Observable:
                elem["type"] = "observable";
                if (val.as_ptr) {
                    elem["kind"] = val.as_observable()->kind_name();
                }
                break;
            case etil::core::Value::Type::Xt:
                elem["type"] = "xt";
                break;
        }

        elements.push_back(elem);
    }

    nlohmann::json stack_data = {
        {"depth", depth},
        {"elements", elements},
        {"status", session.interp->stack_status()}
    };

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", stack_data.dump()}
    });

    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_set_weight(const nlohmann::json& params) {
    auto& session = *current_session_;
    std::string word = params.at("word").get<std::string>();
    double weight = params.at("weight").get<double>();

    auto impls = session.dict->get_implementations(word);
    if (!impls || impls->empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Word not found: " + word}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Determine which implementation to modify
    size_t idx = impls->size() - 1;  // default: latest
    if (params.contains("impl_index") && params["impl_index"].is_number_integer()) {
        int requested = params["impl_index"].get<int>();
        if (requested < 0 || static_cast<size_t>(requested) >= impls->size()) {
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back({
                {"type", "text"},
                {"text", "Invalid impl_index: " + std::to_string(requested) +
                         " (word has " + std::to_string(impls->size()) + " implementations)"}
            });
            return {{"content", content_array}, {"isError", true}};
        }
        idx = static_cast<size_t>(requested);
    }

    (*impls)[idx]->set_weight(weight);

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"word", word},
            {"implIndex", idx},
            {"implName", (*impls)[idx]->name()},
            {"newWeight", weight}
        }).dump()}
    });

    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_reset(const nlohmann::json& /*params*/) {
    auto& session = *current_session_;

    // Shut down old interpreter
    session.interp->shutdown();

    // Release output buffers
    session.out_buf.reset();
    session.interp_out.clear();
    session.err_buf.reset();
    session.interp_err.clear();

    // Recreate dictionary and interpreter (preserve path mapping)
    session.dict = std::make_unique<etil::core::Dictionary>();
    etil::core::register_primitives(*session.dict);
    session.interp = std::make_unique<etil::core::Interpreter>(
        *session.dict, session.interp_out, session.interp_err);
    if (!session.home_dir.empty()) {
        session.interp->set_home_dir(session.home_dir);
    }
    if (!library_dir_.empty()) {
        session.interp->set_library_dir(library_dir_);
    }

    // Recreate LVFS
    if (!session.home_dir.empty()) {
        session.lvfs = std::make_unique<etil::lvfs::Lvfs>(
            session.home_dir, library_dir_);
        session.interp->set_lvfs(session.lvfs.get());
    }

    // Re-wire ExecutionContext pointers to existing session-owned objects
    auto& ctx = session.interp->context();

    if (session.uv_session) {
        ctx.set_uv_session(session.uv_session.get());
    }

#ifdef ETIL_HTTP_CLIENT_ENABLED
    etil::net::register_http_primitives(*session.dict);
    if (session.http_state) {
        ctx.set_http_client_state(session.http_state.get());
    }
#endif

#ifdef ETIL_MONGODB_ENABLED
    etil::db::register_mongo_primitives(*session.dict);
    if (session.mongo_state) {
        ctx.set_mongo_client_state(session.mongo_state.get());
    }
#endif

#ifdef ETIL_JWT_ENABLED
    if (session.permissions_ptr) {
        ctx.set_permissions(session.permissions_ptr);
    }
#endif

    session.interp->register_handler_words();
    session.interp->load_startup_files({"data/builtins.til", "data/help.til"});

    // Discard startup output
    session.out_buf.reset();
    session.interp_out.clear();
    session.err_buf.reset();
    session.interp_err.clear();

    // Reset session profiling counters
    session.stats.reset();

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", "Interpreter reset successfully"}
    });

    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_get_session_stats(
    const nlohmann::json& /*params*/) {
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
        {"type", "text"},
        {"text", stats_json.dump()}
    });

    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_write_file(const nlohmann::json& params) {
    auto& session = *current_session_;

#ifdef ETIL_JWT_ENABLED
    // Check lvfs_modify permission for authenticated sessions
    if (!session.role.empty() && auth_config_) {
        auto* perms = auth_config_->permissions_for(session.user_id);
        if (perms && !perms->lvfs_modify) {
#ifdef ETIL_MONGODB_ENABLED
            if (audit_log_) audit_log_->log_permission_denied(session.email, "write_file", session.role);
#endif
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back({
                {"type", "text"},
                {"text", "Error: file modification not permitted for role '" +
                         session.role + "'"}
            });
            return {{"content", content_array}, {"isError", true}};
        }
    }
#endif

    std::string path = params.at("path").get<std::string>();
    std::string content = params.at("content").get<std::string>();

    // Validate path
    if (path.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: path must not be empty"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    if (path[0] == '/') {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: path must be relative (no leading '/')"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Validate home_dir is configured
    if (session.home_dir.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: session home directory not configured"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Enforce content size limit
    if (content.size() > MCP_MAX_DATA_SIZE) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: content exceeds maximum size (" +
                     std::to_string(MCP_MAX_DATA_SIZE) + " bytes)"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Resolve path under home directory
    std::string resolved = session.interp->resolve_home_path(path);
    if (resolved.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: path traversal rejected for '" + path + "'"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Create parent directories if needed
    namespace fs = std::filesystem;
    fs::path file_path(resolved);
    if (file_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(file_path.parent_path(), ec);
        if (ec) {
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back({
                {"type", "text"},
                {"text", "Error: cannot create parent directories: " + ec.message()}
            });
            return {{"content", content_array}, {"isError", true}};
        }
    }

    // Write the file
    std::ofstream ofs(resolved, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: cannot open file for writing: " + resolved}
        });
        return {{"content", content_array}, {"isError", true}};
    }
    ofs << content;
    ofs.close();

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"path", path},
            {"bytesWritten", content.size()},
            {"resolvedPath", resolved}
        }).dump()}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_read_file(const nlohmann::json& params) {
    auto& session = *current_session_;

    // Reads are always permitted — no permission check needed.

    std::string path = params.at("path").get<std::string>();

    // Validate path
    if (path.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: path must not be empty"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    if (path[0] == '/') {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: path must be relative (no leading '/')"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Validate home_dir is configured
    if (session.home_dir.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: session home directory not configured"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Resolve path under home directory
    std::string resolved = session.interp->resolve_home_path(path);
    if (resolved.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: path traversal rejected for '" + path + "'"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    namespace fs = std::filesystem;
    std::error_code ec;

    // Check file exists and is a regular file
    if (!fs::exists(resolved, ec)) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: file not found: " + path}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    if (!fs::is_regular_file(resolved, ec)) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: not a regular file: " + path}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Check file size
    auto file_size = fs::file_size(resolved, ec);
    if (ec) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: cannot determine file size: " + ec.message()}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    if (file_size > MCP_MAX_DATA_SIZE) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: file exceeds maximum size (" +
                     std::to_string(MCP_MAX_DATA_SIZE) + " bytes)"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Read the file
    std::ifstream ifs(resolved, std::ios::in);
    if (!ifs.is_open()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: cannot open file for reading: " + path}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    ifs.close();

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"path", path},
            {"content", content},
            {"sizeBytes", content.size()}
        }).dump()}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_list_files(const nlohmann::json& params) {
    auto& session = *current_session_;

    // Validate home_dir is configured
    if (session.home_dir.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: session home directory not configured"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::string rel_path = ".";
    if (params.contains("path") && params["path"].is_string()) {
        rel_path = params["path"].get<std::string>();
    }

    // Resolve path under home directory
    std::string resolved;
    if (rel_path == "." || rel_path.empty()) {
        resolved = session.home_dir;
    } else {
        if (rel_path[0] == '/') {
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back({
                {"type", "text"},
                {"text", "Error: path must be relative"}
            });
            return {{"content", content_array}, {"isError", true}};
        }
        resolved = session.interp->resolve_home_path(rel_path);
        if (resolved.empty()) {
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back({
                {"type", "text"},
                {"text", "Error: path traversal rejected for '" + rel_path + "'"}
            });
            return {{"content", content_array}, {"isError", true}};
        }
    }

    namespace fs = std::filesystem;
    nlohmann::json files = nlohmann::json::array();

    std::error_code ec;
    if (!fs::is_directory(resolved, ec)) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", nlohmann::json({{"files", files}}).dump()}
        });
        return {{"content", content_array}};
    }

    for (const auto& entry : fs::directory_iterator(resolved, ec)) {
        nlohmann::json file_entry;
        file_entry["name"] = entry.path().filename().string();
        if (entry.is_directory()) {
            file_entry["type"] = "directory";
        } else {
            file_entry["type"] = "file";
            std::error_code size_ec;
            file_entry["size"] = entry.file_size(size_ec);
        }
        files.push_back(file_entry);
    }

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({{"files", files}}).dump()}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_list_sessions(
    const nlohmann::json& /*params*/) {
    auto& session = *current_session_;

#ifdef ETIL_JWT_ENABLED
    // Check list_sessions permission
    if (!session.role.empty() && auth_config_) {
        auto* perms = auth_config_->permissions_for(session.user_id);
        if (perms && !perms->list_sessions) {
#ifdef ETIL_MONGODB_ENABLED
            if (audit_log_) audit_log_->log_permission_denied(session.email, "list_sessions", session.role);
#endif
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back({
                {"type", "text"},
                {"text", "Error: list_sessions not permitted for role '" +
                         session.role + "'"}
            });
            return {{"content", content_array}, {"isError", true}};
        }
    }
#else
    (void)session;
#endif

    auto now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    nlohmann::json sessions_array = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (const auto& [sid, sess] : sessions_) {
            nlohmann::json entry = {{"session_id", sid}};
#ifdef ETIL_JWT_ENABLED
            entry["user_id"] = sess->user_id;
            entry["role"] = sess->role;
#endif
            uint64_t start_ms = sess->stats.session_start_ms;
            if (start_ms > 0 && now_ms >= start_ms) {
                entry["duration_seconds"] =
                    static_cast<double>(now_ms - start_ms) / 1000.0;
            } else {
                entry["duration_seconds"] = 0.0;
            }
            // Per-role idle timeout info
            auto timeout = effective_timeout(*sess);
            auto idle = std::chrono::steady_clock::now() - sess->last_activity;
            entry["idle_timeout_seconds"] =
                std::chrono::duration_cast<std::chrono::seconds>(timeout)
                    .count();
            entry["idle_seconds"] =
                std::chrono::duration_cast<std::chrono::seconds>(idle).count();
            sessions_array.push_back(entry);
        }
    }

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"sessions", sessions_array},
            {"count", sessions_array.size()}
        }).dump()}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_kick_session(const nlohmann::json& params) {
    auto& session = *current_session_;

#ifdef ETIL_JWT_ENABLED
    // Check session_kick permission
    if (!session.role.empty() && auth_config_) {
        auto* perms = auth_config_->permissions_for(session.user_id);
        if (perms && !perms->session_kick) {
#ifdef ETIL_MONGODB_ENABLED
            if (audit_log_) audit_log_->log_permission_denied(session.email, "kick_session", session.role);
#endif
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back({
                {"type", "text"},
                {"text", "Error: kick_session not permitted for role '" +
                         session.role + "'"}
            });
            return {{"content", content_array}, {"isError", true}};
        }
    }
#endif

    std::string target_id = params.at("session_id").get<std::string>();

    // Prevent self-kick
    if (target_id == session.id) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: cannot kick your own session"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Find and erase the target session
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(target_id);
        if (it != sessions_.end()) {
            sessions_.erase(it);
            found = true;
        }
    }

    nlohmann::json content_array = nlohmann::json::array();
    if (found) {
        content_array.push_back({
            {"type", "text"},
            {"text", nlohmann::json({
                {"kicked", true},
                {"session_id", target_id}
            }).dump()}
        });
    } else {
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: session not found: " + target_id}
        });
        return {{"content", content_array}, {"isError", true}};
    }
    return {{"content", content_array}};
}

#if defined(ETIL_HTTP_CLIENT_ENABLED) && defined(ETIL_JWT_ENABLED)
nlohmann::json McpServer::tool_manage_allowlist(const nlohmann::json& params) {
    auto& session = *current_session_;

    // Check allowlist_admin permission
    if (!session.role.empty() && auth_config_) {
        auto* perms = auth_config_->permissions_for(session.user_id);
        if (perms && !perms->allowlist_admin) {
#ifdef ETIL_MONGODB_ENABLED
            if (audit_log_) audit_log_->log_permission_denied(session.email, "manage_allowlist", session.role);
#endif
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back({
                {"type", "text"},
                {"text", "Error: manage_allowlist not permitted for role '" +
                         session.role + "'"}
            });
            return {{"content", content_array}, {"isError", true}};
        }
    }

    std::string action = params.at("action").get<std::string>();

    // Must have an HTTP state with a role config to mutate
    if (!session.http_state || !session.role_http_config) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: HTTP client not configured for this session"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    auto& domains = session.role_http_config->allowed_domains;

    if (action == "add") {
        std::string domain = params.at("domain").get<std::string>();
        // Avoid duplicates
        if (std::find(domains.begin(), domains.end(), domain) == domains.end()) {
            domains.push_back(domain);
        }
    } else if (action == "remove") {
        std::string domain = params.at("domain").get<std::string>();
        auto it = std::find(domains.begin(), domains.end(), domain);
        if (it != domains.end()) {
            domains.erase(it);
        }
    } else if (action != "list") {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: invalid action '" + action +
                     "' (expected add, remove, or list)"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Return current domain list
    nlohmann::json domain_list = nlohmann::json::array();
    for (const auto& d : domains) {
        domain_list.push_back(d);
    }

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"domains", domain_list},
            {"count", domain_list.size()}
        }).dump()}
    });
    return {{"content", content_array}};
}
#endif

// ---------------------------------------------------------------------------
// Admin tools — role/user management (gated by ETIL_JWT_ENABLED)
// ---------------------------------------------------------------------------

#ifdef ETIL_JWT_ENABLED

namespace {

/// Check role_admin permission.  Returns an error JSON if denied, nullopt if OK.
std::optional<nlohmann::json> check_role_admin(
    const Session& session,
    const std::shared_ptr<const AuthConfig>& auth_config,
    const char* tool_name
#ifdef ETIL_MONGODB_ENABLED
    , etil::aaa::AuditLog* audit_log
#endif
) {
    if (!session.role.empty() && auth_config) {
        auto* perms = auth_config->permissions_for(session.user_id);
        if (perms && !perms->role_admin) {
#ifdef ETIL_MONGODB_ENABLED
            if (audit_log)
                audit_log->log_permission_denied(session.email, tool_name,
                                                 session.role);
#endif
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back({
                {"type", "text"},
                {"text", std::string("Error: ") + tool_name +
                         " not permitted for role '" + session.role + "'"}
            });
            return nlohmann::json{{"content", content_array}, {"isError", true}};
        }
    }
    return std::nullopt;
}

} // anonymous namespace

nlohmann::json McpServer::tool_admin_list_roles(
    const nlohmann::json& /*params*/) {
    auto& session = *current_session_;
    auto denied = check_role_admin(session, auth_config_, "admin_list_roles"
#ifdef ETIL_MONGODB_ENABLED
        , audit_log_.get()
#endif
    );
    if (denied) return *denied;

    if (!auth_config_) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: no auth configuration loaded"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    nlohmann::json roles_array = nlohmann::json::array();
    for (const auto& [name, _] : auth_config_->roles) {
        roles_array.push_back(name);
    }
    std::sort(roles_array.begin(), roles_array.end());

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"roles", roles_array},
            {"default_role", auth_config_->default_role},
            {"count", roles_array.size()}
        }).dump(2)}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_admin_get_role(const nlohmann::json& params) {
    auto& session = *current_session_;
    auto denied = check_role_admin(session, auth_config_, "admin_get_role"
#ifdef ETIL_MONGODB_ENABLED
        , audit_log_.get()
#endif
    );
    if (denied) return *denied;

    if (!auth_config_) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: no auth configuration loaded"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::string role_name = params.at("role").get<std::string>();
    auto it = auth_config_->roles.find(role_name);
    if (it == auth_config_->roles.end()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: role not found: " + role_name}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Build a temporary single-role config to reuse roles_to_json() format
    nlohmann::json role_json;
    AuthConfig tmp;
    tmp.roles[role_name] = it->second;
    auto j = tmp.roles_to_json();
    role_json = j["roles"][role_name];

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"role", role_name},
            {"permissions", role_json},
            {"is_default", role_name == auth_config_->default_role}
        }).dump(2)}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_admin_set_role(const nlohmann::json& params) {
    auto& session = *current_session_;
    auto denied = check_role_admin(session, auth_config_, "admin_set_role"
#ifdef ETIL_MONGODB_ENABLED
        , audit_log_.get()
#endif
    );
    if (denied) return *denied;

    if (!auth_config_ || auth_config_dir_.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: no auth configuration loaded or directory not set"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::string role_name = params.at("role").get<std::string>();
    auto perms_json = params.at("permissions");
    auto new_perms = AuthConfig::parse_role_permissions(perms_json);

    // Copy-on-write: copy current config, mutate, replace atomically
    auto new_config = std::make_shared<AuthConfig>(*auth_config_);
    bool is_new = new_config->roles.find(role_name) == new_config->roles.end();
    new_config->roles[role_name] = new_perms;

    // Persist to disk
    try {
        AuthConfig::write_json_atomic(
            auth_config_dir_ + "/roles.json",
            new_config->roles_to_json());
    } catch (const std::exception& e) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", std::string("Error: failed to persist roles.json: ") +
                     e.what()}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Atomically swap the config
    std::atomic_store(&auth_config_, std::shared_ptr<const AuthConfig>(new_config));

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"action", is_new ? "created" : "updated"},
            {"role", role_name}
        }).dump()}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_admin_delete_role(const nlohmann::json& params) {
    auto& session = *current_session_;
    auto denied = check_role_admin(session, auth_config_, "admin_delete_role"
#ifdef ETIL_MONGODB_ENABLED
        , audit_log_.get()
#endif
    );
    if (denied) return *denied;

    if (!auth_config_ || auth_config_dir_.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: no auth configuration loaded or directory not set"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::string role_name = params.at("role").get<std::string>();

    // Cannot delete the default role
    if (role_name == auth_config_->default_role) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: cannot delete the default role '" + role_name + "'"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    // Check no users are assigned to this role
    for (const auto& [user_id, role] : auth_config_->user_roles) {
        if (role == role_name) {
            nlohmann::json content_array = nlohmann::json::array();
            content_array.push_back({
                {"type", "text"},
                {"text", "Error: cannot delete role '" + role_name +
                         "' — user '" + user_id + "' is still assigned to it"}
            });
            return {{"content", content_array}, {"isError", true}};
        }
    }

    auto new_config = std::make_shared<AuthConfig>(*auth_config_);
    auto it = new_config->roles.find(role_name);
    if (it == new_config->roles.end()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: role not found: " + role_name}
        });
        return {{"content", content_array}, {"isError", true}};
    }
    new_config->roles.erase(it);

    try {
        AuthConfig::write_json_atomic(
            auth_config_dir_ + "/roles.json",
            new_config->roles_to_json());
    } catch (const std::exception& e) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", std::string("Error: failed to persist roles.json: ") +
                     e.what()}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::atomic_store(&auth_config_, std::shared_ptr<const AuthConfig>(new_config));

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"action", "deleted"},
            {"role", role_name}
        }).dump()}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_admin_list_users(
    const nlohmann::json& /*params*/) {
    auto& session = *current_session_;
    auto denied = check_role_admin(session, auth_config_, "admin_list_users"
#ifdef ETIL_MONGODB_ENABLED
        , audit_log_.get()
#endif
    );
    if (denied) return *denied;

    if (!auth_config_) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: no auth configuration loaded"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    nlohmann::json users_array = nlohmann::json::array();
    for (const auto& [user_id, role] : auth_config_->user_roles) {
        users_array.push_back({{"user_id", user_id}, {"role", role}});
    }

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"users", users_array},
            {"default_role", auth_config_->default_role},
            {"count", users_array.size()}
        }).dump(2)}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_admin_set_user_role(
    const nlohmann::json& params) {
    auto& session = *current_session_;
    auto denied = check_role_admin(session, auth_config_, "admin_set_user_role"
#ifdef ETIL_MONGODB_ENABLED
        , audit_log_.get()
#endif
    );
    if (denied) return *denied;

    if (!auth_config_ || auth_config_dir_.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: no auth configuration loaded or directory not set"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::string user_id = params.at("user_id").get<std::string>();
    std::string role = params.at("role").get<std::string>();

    // Validate role exists
    if (auth_config_->roles.find(role) == auth_config_->roles.end()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: role not found: " + role +
                     " (create it first with admin_set_role)"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    auto new_config = std::make_shared<AuthConfig>(*auth_config_);
    new_config->user_roles[user_id] = role;

    try {
        AuthConfig::write_json_atomic(
            auth_config_dir_ + "/users.json",
            new_config->users_to_json());
    } catch (const std::exception& e) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", std::string("Error: failed to persist users.json: ") +
                     e.what()}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::atomic_store(&auth_config_, std::shared_ptr<const AuthConfig>(new_config));

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"user_id", user_id},
            {"role", role}
        }).dump()}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_admin_delete_user(
    const nlohmann::json& params) {
    auto& session = *current_session_;
    auto denied = check_role_admin(session, auth_config_, "admin_delete_user"
#ifdef ETIL_MONGODB_ENABLED
        , audit_log_.get()
#endif
    );
    if (denied) return *denied;

    if (!auth_config_ || auth_config_dir_.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: no auth configuration loaded or directory not set"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::string user_id = params.at("user_id").get<std::string>();

    auto new_config = std::make_shared<AuthConfig>(*auth_config_);
    auto it = new_config->user_roles.find(user_id);
    if (it == new_config->user_roles.end()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: user not found: " + user_id}
        });
        return {{"content", content_array}, {"isError", true}};
    }
    std::string old_role = it->second;
    new_config->user_roles.erase(it);

    try {
        AuthConfig::write_json_atomic(
            auth_config_dir_ + "/users.json",
            new_config->users_to_json());
    } catch (const std::exception& e) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", std::string("Error: failed to persist users.json: ") +
                     e.what()}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::atomic_store(&auth_config_, std::shared_ptr<const AuthConfig>(new_config));

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"action", "deleted"},
            {"user_id", user_id},
            {"previous_role", old_role},
            {"now_defaults_to", new_config->default_role}
        }).dump()}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_admin_set_default_role(
    const nlohmann::json& params) {
    auto& session = *current_session_;
    auto denied = check_role_admin(session, auth_config_, "admin_set_default_role"
#ifdef ETIL_MONGODB_ENABLED
        , audit_log_.get()
#endif
    );
    if (denied) return *denied;

    if (!auth_config_ || auth_config_dir_.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: no auth configuration loaded or directory not set"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::string role_name = params.at("role").get<std::string>();

    // Validate that the role exists
    if (auth_config_->roles.find(role_name) == auth_config_->roles.end()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: role not found: " + role_name}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    auto new_config = std::make_shared<AuthConfig>(*auth_config_);
    std::string previous = new_config->default_role;
    new_config->default_role = role_name;

    try {
        AuthConfig::write_json_atomic(
            auth_config_dir_ + "/roles.json",
            new_config->roles_to_json());
    } catch (const std::exception& e) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", std::string("Error: failed to persist roles.json: ") +
                     e.what()}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    std::atomic_store(&auth_config_, std::shared_ptr<const AuthConfig>(new_config));

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"action", "default_role_changed"},
            {"previous", previous},
            {"default_role", role_name}
        }).dump()}
    });
    return {{"content", content_array}};
}

nlohmann::json McpServer::tool_admin_reload_config(
    const nlohmann::json& /*params*/) {
    auto& session = *current_session_;
    auto denied = check_role_admin(session, auth_config_, "admin_reload_config"
#ifdef ETIL_MONGODB_ENABLED
        , audit_log_.get()
#endif
    );
    if (denied) return *denied;

    if (auth_config_dir_.empty()) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", "Error: no auth config directory configured"}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    try {
        auto new_config = std::make_shared<const AuthConfig>(
            AuthConfig::from_directory(auth_config_dir_));
        std::atomic_store(&auth_config_, new_config);
    } catch (const std::exception& e) {
        nlohmann::json content_array = nlohmann::json::array();
        content_array.push_back({
            {"type", "text"},
            {"text", std::string("Error: failed to reload config: ") + e.what()}
        });
        return {{"content", content_array}, {"isError", true}};
    }

    nlohmann::json content_array = nlohmann::json::array();
    content_array.push_back({
        {"type", "text"},
        {"text", nlohmann::json({
            {"action", "reloaded"},
            {"directory", auth_config_dir_},
            {"roles_count", auth_config_->roles.size()},
            {"users_count", auth_config_->user_roles.size()}
        }).dump()}
    });
    return {{"content", content_array}};
}

#endif // ETIL_JWT_ENABLED

} // namespace etil::mcp
