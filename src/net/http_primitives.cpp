// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/net/http_primitives.hpp"
#include "etil/net/http_client_config.hpp"
#include "etil/net/url_validation.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_byte_array.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/fileio/uv_session.hpp"
#include "etil/mcp/role_permissions.hpp"

#include <httplib.h>

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace etil::net {

namespace {


/// Data shared between the interpreter thread and the libuv thread-pool
/// worker that performs the blocking HTTP request.
struct HttpWorkData {
    // --- Input (set by caller, read by worker) ---
    std::string scheme_host_port;  // e.g. "https://example.com:443"
    std::string path;              // e.g. "/data/file.csv"
    int timeout_ms = 10'000;
    size_t max_response_bytes = 1 * 1024 * 1024;
    httplib::Headers headers;      // extra HTTP headers from HeapMap

    // --- Output (set by worker, read by caller) ---
    std::vector<uint8_t> body;
    int status_code = 0;
    std::string error;

    // --- Cancellation ---
    std::atomic<bool> cancelled{false};
};

/// Runs on libuv thread pool.  Performs the blocking HTTP GET request.
void http_get_work(uv_work_t* req) {
    auto* wr = static_cast<etil::fileio::WorkRequest*>(req->data);
    auto* d = static_cast<HttpWorkData*>(wr->user_data);

    try {
        httplib::Client cli(d->scheme_host_port);

        auto timeout_sec = d->timeout_ms / 1000;
        auto timeout_usec = (d->timeout_ms % 1000) * 1000;
        cli.set_connection_timeout(timeout_sec, timeout_usec);
        cli.set_read_timeout(timeout_sec, timeout_usec);
        cli.set_write_timeout(timeout_sec, timeout_usec);
        cli.set_follow_location(false);  // No auto-redirect (SSRF safety)

        size_t received = 0;
        auto result = cli.Get(
            d->path,
            d->headers,
            [&](const char* data, size_t len) -> bool {
                if (d->cancelled.load(std::memory_order_relaxed)) return false;
                if (received + len > d->max_response_bytes) return false;
                d->body.insert(d->body.end(), data, data + len);
                received += len;
                return true;
            });

        if (result) {
            d->status_code = result->status;
        } else {
            d->error = httplib::to_string(result.error());
        }
    } catch (const std::exception& e) {
        d->error = e.what();
    }
}

}  // namespace

// http-get ( url headers-map -- bytes status-code flag )
//
// Perform an HTTP/HTTPS GET request with extra headers.  Returns:
//   - Response body as HeapByteArray (opaque bytes, NOT a string)
//   - HTTP status code (integer)
//   - Success flag (true = success, false = failure)
// On failure, pushes only flag=false.
// headers-map is a HeapMap of string key→string value pairs.
static bool prim_http_get(etil::core::ExecutionContext& ctx) {
    using namespace etil::core;

    // Pop headers map (TOS)
    auto hdr_opt = ctx.data_stack().pop();
    if (!hdr_opt) return false;
    if (hdr_opt->type != Value::Type::Map || !hdr_opt->as_ptr) {
        ctx.err() << "Error: http-get: expected Map for headers\n";
        value_release(*hdr_opt);
        return false;
    }
    auto* hdr_map = hdr_opt->as_map();

    // Pop URL string
    auto* hs = pop_string(ctx);
    if (!hs) { hdr_map->release(); return false; }
    std::string url(hs->view());
    hs->release();

    // Helper: release map and push failure flag
    auto fail = [&](const char* msg) {
        ctx.err() << msg;
        hdr_map->release();
        ctx.data_stack().push(Value(false));
    };

    // Check net_client_allowed permission
    auto* perms = ctx.permissions();
    if (perms && !perms->net_client_allowed) {
        fail("Error: http-get: not permitted\n");
        return true;
    }

    // Check HTTP client state
    auto* http_state = ctx.http_client_state();
    if (!http_state || !http_state->config || !http_state->config->enabled()) {
        fail("Error: http-get: HTTP client not configured "
             "(set ETIL_HTTP_ALLOWLIST env var)\n");
        return true;
    }

    // Check per-interpret and lifetime budgets
    if (!http_state->can_fetch()) {
        ctx.err() << "Error: http-get: fetch budget exceeded ("
                  << http_state->per_interpret_fetches << "/"
                  << http_state->config->per_interpret_budget
                  << " per-interpret, "
                  << http_state->lifetime_fetches << "/"
                  << http_state->config->per_session_budget
                  << " lifetime)\n";
        hdr_map->release();
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Validate URL (parse, scheme check, allowlist, DNS + SSRF blocklist)
    ParsedUrl parsed;
    std::string error;
    if (!validate_url(url, *http_state->config, parsed, error)) {
        ctx.err() << "Error: http-get: " << error << "\n";
        hdr_map->release();
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Need UvSession for async execution
    auto* uv = ctx.uv_session();
    if (!uv) {
        fail("Error: http-get: no UvSession (async I/O not available)\n");
        return true;
    }

    // Build scheme_host_port for cpp-httplib Client constructor
    std::string scheme_host_port = parsed.scheme + "://" + parsed.host;
    if (parsed.port != 0) {
        scheme_host_port += ":" + std::to_string(parsed.port);
    }

    // Set up work data
    HttpWorkData work_data;
    work_data.scheme_host_port = std::move(scheme_host_port);
    work_data.path = parsed.path;
    work_data.timeout_ms = http_state->config->request_timeout_ms;
    work_data.max_response_bytes = http_state->config->max_response_bytes;

    // Convert HeapMap entries to httplib::Headers
    for (const auto& [key, val] : hdr_map->entries()) {
        if (val.type == Value::Type::String && val.as_ptr) {
            work_data.headers.emplace(key, std::string(val.as_string()->view()));
        }
    }
    hdr_map->release();

    // Submit to libuv thread pool
    etil::fileio::WorkRequest work_req;
    work_req.user_data = &work_data;

    int r = uv_queue_work(uv->loop(), &work_req.req,
                          http_get_work,
                          etil::fileio::WorkRequest::on_after_work);
    if (r != 0) {
        ctx.err() << "Error: http-get: failed to queue work: "
                  << uv_strerror(r) << "\n";
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Await completion with tick()-based cancellation
    bool completed = uv->await_work(ctx, work_req, &work_data.cancelled);

    // Record the fetch regardless of outcome
    http_state->record_fetch();

    if (!completed) {
        // Execution limit hit (timeout/budget/cancellation)
        return false;
    }

    // Check for HTTP client error
    if (!work_data.error.empty()) {
        ctx.err() << "Error: http-get: " << work_data.error << "\n";
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Push result: bytes status-code flag
    auto* ba = new HeapByteArray(work_data.body.size());
    if (!work_data.body.empty()) {
        std::memcpy(ba->data(), work_data.body.data(), work_data.body.size());
    }
    ba->set_tainted(true);  // Network data is untrusted
    ctx.data_stack().push(Value::from(ba));
    ctx.data_stack().push(Value(static_cast<int64_t>(work_data.status_code)));
    ctx.data_stack().push(Value(true));
    return true;
}

void register_http_primitives(etil::core::Dictionary& dict) {
    using namespace etil::core;
    using TS = TypeSignature;
    using T = TS::Type;

    auto make_word = [](const char* name, WordImpl::FunctionPtr fn,
                        std::vector<T> inputs, std::vector<T> outputs) {
        return make_primitive(name, fn, std::move(inputs), std::move(outputs));
    };

    dict.register_word("http-get",
        make_word("prim_http_get", prim_http_get,
            {T::String, T::Unknown}, {T::Unknown, T::Integer, T::Integer}));
}

} // namespace etil::net
