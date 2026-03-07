#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace etil::mcp {

/// JSON-RPC 2.0 error codes.
enum class JsonRpcError : int {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
};

/// A parsed JSON-RPC 2.0 request.
struct JsonRpcRequest {
    std::string jsonrpc;                    // Must be "2.0"
    std::optional<nlohmann::json> id;       // null for notifications
    std::string method;
    nlohmann::json params;                  // object or array, defaults to {}
};

/// Parse a JSON string into a JsonRpcRequest.
/// Returns nullopt if the input is not valid JSON-RPC 2.0.
/// On failure, `error_out` is set to an appropriate error response JSON.
std::optional<JsonRpcRequest> parse_request(const std::string& json_str,
                                            nlohmann::json& error_out);

/// Parse a JSON object into a JsonRpcRequest.
/// Returns nullopt if the object is not valid JSON-RPC 2.0.
std::optional<JsonRpcRequest> parse_request(const nlohmann::json& j,
                                            nlohmann::json& error_out);

/// Build a JSON-RPC 2.0 success response.
nlohmann::json make_response(const nlohmann::json& id,
                             const nlohmann::json& result);

/// Build a JSON-RPC 2.0 error response.
nlohmann::json make_error(const nlohmann::json& id,
                          int code,
                          const std::string& message,
                          const nlohmann::json& data = nullptr);

/// Convenience: build error response from JsonRpcError enum.
nlohmann::json make_error(const nlohmann::json& id,
                          JsonRpcError code,
                          const std::string& message,
                          const nlohmann::json& data = nullptr);

} // namespace etil::mcp
