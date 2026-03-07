// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/auth_config.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace etil::mcp {

namespace {

/// Read entire file into a string.  Throws on failure.
std::string read_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

/// Try to read a JSON file.  Returns nullopt if the file doesn't exist.
/// Throws on parse errors.
std::optional<nlohmann::json> try_read_json(const std::string& path,
                                             const std::string& label) {
    if (!std::filesystem::exists(path)) return std::nullopt;
    auto content = read_file(path);
    try {
        return nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse " + label + " '" + path +
                                 "': " + e.what());
    }
}

// --- Field-reading helpers ---

void read_bool(const nlohmann::json& obj, const char* key, bool& out) {
    if (obj.contains(key) && obj[key].is_boolean()) {
        out = obj[key].get<bool>();
    }
}

void read_int(const nlohmann::json& obj, const char* key, int& out) {
    if (obj.contains(key) && obj[key].is_number_integer()) {
        out = obj[key].get<int>();
    }
}

void read_int64(const nlohmann::json& obj, const char* key, int64_t& out) {
    if (obj.contains(key) && obj[key].is_number_integer()) {
        out = obj[key].get<int64_t>();
    }
}

void read_string_array(const nlohmann::json& obj, const char* key,
                       std::vector<std::string>& out) {
    if (obj.contains(key) && obj[key].is_array()) {
        for (const auto& elem : obj[key]) {
            if (elem.is_string()) {
                out.push_back(elem.get<std::string>());
            }
        }
    }
}

// --- Section parsers ---

/// Parse "roles" and "default_role" from JSON into config.
void parse_roles(AuthConfig& config, const nlohmann::json& j) {
    if (j.contains("roles") && j["roles"].is_object()) {
        for (auto& [role_name, role_json] : j["roles"].items()) {
            RolePermissions perms;

            // --- System ---
            read_int(role_json, "max_sessions", perms.max_sessions);
            read_int(role_json, "instruction_budget", perms.instruction_budget);
            read_bool(role_json, "allowlist_admin", perms.allowlist_admin);
            read_bool(role_json, "list_sessions", perms.list_sessions);
            read_bool(role_json, "session_kick", perms.session_kick);
            read_bool(role_json, "send_system_notification",
                      perms.send_system_notification);
            read_bool(role_json, "send_user_notification",
                      perms.send_user_notification);

            // --- LVFS ---
            // Backward compat: file_io → lvfs_modify
            bool has_file_io = role_json.contains("file_io") &&
                               role_json["file_io"].is_boolean();
            if (has_file_io) {
                perms.lvfs_modify = role_json["file_io"].get<bool>();
            }
            // New name takes precedence
            read_bool(role_json, "lvfs_modify", perms.lvfs_modify);
            read_int64(role_json, "disk_quota", perms.disk_quota);

            // --- Network Client ---
            // Backward compat: http_domains → net_client_domains
            read_string_array(role_json, "http_domains",
                              perms.net_client_domains);
            // New name overwrites if present
            if (role_json.contains("net_client_domains") &&
                role_json["net_client_domains"].is_array()) {
                perms.net_client_domains.clear();
                read_string_array(role_json, "net_client_domains",
                                  perms.net_client_domains);
            }
            read_bool(role_json, "net_client_allowed",
                      perms.net_client_allowed);
            // Infer net_client_allowed from non-empty domains
            if (!perms.net_client_domains.empty() &&
                !role_json.contains("net_client_allowed")) {
                perms.net_client_allowed = true;
            }
            read_int(role_json, "net_client_quota", perms.net_client_quota);

            // --- Network Server (schema only) ---
            read_bool(role_json, "net_server_bind", perms.net_server_bind);
            read_bool(role_json, "net_server_tcp", perms.net_server_tcp);
            read_bool(role_json, "net_server_udp", perms.net_server_udp);

            // --- Code Execution ---
            read_bool(role_json, "evaluate", perms.evaluate);
            read_bool(role_json, "evaluate_tainted", perms.evaluate_tainted);

            // --- Database (MongoDB) ---
            read_bool(role_json, "mongo_access", perms.mongo_access);
            read_int(role_json, "mongo_query_quota", perms.mongo_query_quota);

            config.roles[role_name] = std::move(perms);
        }
    }

    // Default role
    if (j.contains("default_role") && j["default_role"].is_string()) {
        config.default_role = j["default_role"].get<std::string>();
    }
}

/// Parse "user_roles" from JSON into config.
void parse_users(AuthConfig& config, const nlohmann::json& j) {
    if (j.contains("user_roles") && j["user_roles"].is_object()) {
        for (auto& [user_id, role] : j["user_roles"].items()) {
            if (role.is_string()) {
                config.user_roles[user_id] = role.get<std::string>();
            }
        }
    }
}

/// Parse JWT keys, TTL, and OAuth providers from JSON into config.
void parse_keys(AuthConfig& config, const nlohmann::json& j) {
    // JWT key files — read PEM content from referenced files
    if (j.contains("jwt_private_key_file") &&
        j["jwt_private_key_file"].is_string()) {
        config.jwt_private_key =
            read_file(j["jwt_private_key_file"].get<std::string>());
    }

    if (j.contains("jwt_public_key_file") &&
        j["jwt_public_key_file"].is_string()) {
        config.jwt_public_key =
            read_file(j["jwt_public_key_file"].get<std::string>());
    }

    // TTL
    if (j.contains("jwt_ttl_seconds") &&
        j["jwt_ttl_seconds"].is_number_integer()) {
        config.jwt_ttl_seconds = j["jwt_ttl_seconds"].get<int>();
    }

    // Parse OAuth providers
    if (j.contains("providers") && j["providers"].is_object()) {
        for (auto& [provider_name, provider_json] : j["providers"].items()) {
            OAuthProviderConfig prov;

            if (provider_json.contains("enabled") &&
                provider_json["enabled"].is_boolean()) {
                prov.enabled = provider_json["enabled"].get<bool>();
            }

            if (provider_json.contains("client_id") &&
                provider_json["client_id"].is_string()) {
                prov.client_id = provider_json["client_id"].get<std::string>();
            }

            if (provider_json.contains("client_secret") &&
                provider_json["client_secret"].is_string()) {
                prov.client_secret =
                    provider_json["client_secret"].get<std::string>();
            }

            config.providers[provider_name] = std::move(prov);
        }
    }
}

} // anonymous namespace

AuthConfig AuthConfig::from_directory(const std::string& dir_path) {
    AuthConfig config;

    auto roles_json = try_read_json(dir_path + "/roles.json", "roles config");
    auto keys_json = try_read_json(dir_path + "/keys.json", "keys config");
    auto users_json = try_read_json(dir_path + "/users.json", "users config");

    if (roles_json) parse_roles(config, *roles_json);
    if (users_json) parse_users(config, *users_json);
    if (keys_json)  parse_keys(config, *keys_json);

    return config;
}

AuthConfig AuthConfig::from_file(const std::string& path) {
    auto content = read_file(path);

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse auth config '" + path +
                                 "': " + e.what());
    }

    AuthConfig config;
    parse_roles(config, j);
    parse_users(config, j);
    parse_keys(config, j);

    return config;
}

std::string AuthConfig::role_for(const std::string& user_id) const {
    auto it = user_roles.find(user_id);
    if (it != user_roles.end()) {
        return it->second;
    }
    return default_role;
}

const RolePermissions*
AuthConfig::permissions_for(const std::string& user_id) const {
    auto role = role_for(user_id);
    auto it = roles.find(role);
    if (it != roles.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
