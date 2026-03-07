// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/json_rpc.hpp"

namespace etil::mcp {

std::optional<JsonRpcRequest> parse_request(const std::string& json_str,
                                            nlohmann::json& error_out) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_str);
    } catch (const nlohmann::json::parse_error&) {
        error_out = make_error(nullptr, JsonRpcError::ParseError,
                               "Parse error: invalid JSON");
        return std::nullopt;
    }
    return parse_request(j, error_out);
}

std::optional<JsonRpcRequest> parse_request(const nlohmann::json& j,
                                            nlohmann::json& error_out) {
    if (!j.is_object()) {
        error_out = make_error(nullptr, JsonRpcError::InvalidRequest,
                               "Invalid Request: expected JSON object");
        return std::nullopt;
    }

    // Extract id (may be absent for notifications)
    std::optional<nlohmann::json> id;
    if (j.contains("id")) {
        id = j["id"];
    }

    auto id_val = id.value_or(nullptr);

    // Validate jsonrpc field
    if (!j.contains("jsonrpc") || !j["jsonrpc"].is_string() ||
        j["jsonrpc"].get<std::string>() != "2.0") {
        error_out = make_error(id_val, JsonRpcError::InvalidRequest,
                               "Invalid Request: missing or wrong jsonrpc version");
        return std::nullopt;
    }

    // Validate method field
    if (!j.contains("method") || !j["method"].is_string()) {
        error_out = make_error(id_val, JsonRpcError::InvalidRequest,
                               "Invalid Request: missing or non-string method");
        return std::nullopt;
    }

    JsonRpcRequest req;
    req.jsonrpc = "2.0";
    req.id = id;
    req.method = j["method"].get<std::string>();

    // params is optional, defaults to empty object
    if (j.contains("params")) {
        if (!j["params"].is_object() && !j["params"].is_array()) {
            error_out = make_error(id_val, JsonRpcError::InvalidRequest,
                                   "Invalid Request: params must be object or array");
            return std::nullopt;
        }
        req.params = j["params"];
    } else {
        req.params = nlohmann::json::object();
    }

    return req;
}

nlohmann::json make_response(const nlohmann::json& id,
                             const nlohmann::json& result) {
    return nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

nlohmann::json make_error(const nlohmann::json& id,
                          int code,
                          const std::string& message,
                          const nlohmann::json& data) {
    nlohmann::json err = {
        {"code", code},
        {"message", message}
    };
    if (!data.is_null()) {
        err["data"] = data;
    }
    return nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", err}
    };
}

nlohmann::json make_error(const nlohmann::json& id,
                          JsonRpcError code,
                          const std::string& message,
                          const nlohmann::json& data) {
    return make_error(id, static_cast<int>(code), message, data);
}

} // namespace etil::mcp
