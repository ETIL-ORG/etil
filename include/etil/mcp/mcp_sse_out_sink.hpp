#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Manifold sink that dispatches outbound MCP notifications onto the
/// SSE stream via the existing McpServer emit_notification /
/// send_targeted_notification methods.
///
/// Used as the terminal sink on a route that matches
/// etil.mcp.out.notification.** . The notification_sender_ closures
/// installed per interpret() publish onto those channels; this sink
/// bridges channel messages back to the legacy transport layer
/// (preserved verbatim for Phase 1 — transport-layer rewrites move to
/// later phases).

#include <functional>
#include <memory>
#include <string>

#include "etil/manifold/sink.hpp"

namespace etil::mcp {

class McpServer;

/// Create a sink that, on accept(), inspects the message:
///   - If tags["target_user_id"] is present → call
///     McpServer::send_targeted_notification(target_user_id, payload).
///   - Else → call McpServer::emit_notification(payload).
///
/// The sink holds a raw pointer to McpServer. Caller owns McpServer's
/// lifetime; remove the route before destroying the server.
std::shared_ptr<etil::manifold::ISink> make_mcp_sse_out_sink(McpServer* server);

} // namespace etil::mcp
