// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/mcp_sse_out_sink.hpp"

#include <any>
#include <string>
#include <typeindex>

#include "etil/mcp/mcp_server.hpp"

namespace etil::mcp {

namespace {

class McpSseOutSink : public etil::manifold::ISink {
public:
    explicit McpSseOutSink(McpServer* server) : server_(server) {}

    void accept(const etil::manifold::Message& msg) override {
        if (!server_) return;

        std::string payload;
        if (msg.payload.type() == typeid(std::string)) {
            try {
                payload = std::any_cast<std::string>(msg.payload);
            } catch (...) { return; }
        } else {
            return;  // non-string payloads ignored for now
        }

        auto target_it = msg.tags.find("target_user_id");
        if (target_it != msg.tags.end() && !target_it->second.empty()) {
            server_->send_targeted_notification(target_it->second, payload);
        } else {
            server_->emit_notification(payload);
        }
    }

private:
    McpServer* server_;
};

} // namespace

std::shared_ptr<etil::manifold::ISink> make_mcp_sse_out_sink(McpServer* server) {
    return std::make_shared<McpSseOutSink>(server);
}

} // namespace etil::mcp
