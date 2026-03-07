// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/mcp_server.hpp"
#include "etil/mcp/http_transport.hpp"
#include "etil/core/version.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>

static void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [OPTIONS]\n"
        << "\n"
        << "Options:\n"
        << "  --host <addr>    HTTP listen address (default: 0.0.0.0)\n"
        << "  --port <port>    HTTP port (default: 8080)\n"
        << "  -h, --help       Print this help and exit\n"
        << "  -v, --version    Print version and exit\n";
}

int main(int argc, char* argv[]) {
    std::string host = "0.0.0.0";
    int port = 8080;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--host") == 0) {
            if (i + 1 < argc) {
                host = argv[++i];
            } else {
                std::cerr << "--host requires an address argument\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) {
                port = std::atoi(argv[++i]);
            } else {
                std::cerr << "--port requires a number argument\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "-v") == 0 ||
                   std::strcmp(argv[i], "--version") == 0) {
            std::cerr << "etil_mcp_server " << etil::core::ETIL_VERSION << "\n";
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Ignore SIGPIPE so broken pipes (client disconnects) produce EPIPE
    // on write instead of killing the process silently.
    signal(SIGPIPE, SIG_IGN);

    try {
        etil::mcp::McpServer server;

        etil::mcp::HttpTransportConfig config;
        config.host = host;
        config.port = port;

        // Read API key from environment variable
        const char* api_key_env = std::getenv("ETIL_MCP_API_KEY");
        if (api_key_env && api_key_env[0] != '\0') {
            config.api_key = api_key_env;
            std::cerr << "API key authentication enabled\n";
        }

        etil::mcp::HttpTransport transport(config);
        server.run_http(transport);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Fatal unknown error\n";
        return 1;
    }

    return 0;
}
