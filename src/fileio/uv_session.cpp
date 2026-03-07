// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/fileio/uv_session.hpp"
#include "etil/core/execution_context.hpp"

#include <thread>

namespace etil::fileio {

UvSession::UvSession() {
    uv_loop_init(&loop_);
}

UvSession::~UvSession() {
    // Walk all handles and close them, then drain the loop.
    uv_walk(&loop_, [](uv_handle_t* handle, void*) {
        if (!uv_is_closing(handle)) {
            uv_close(handle, nullptr);
        }
    }, nullptr);
    drain();
    uv_loop_close(&loop_);
}

bool UvSession::await_completion(etil::core::ExecutionContext& ctx,
                                  FsRequest& req) {
    while (!req.done.load(std::memory_order_acquire)) {
        // Poll the event loop — processes completed callbacks.
        uv_run(&loop_, UV_RUN_NOWAIT);

        // Check if the request completed during this poll.
        if (req.done.load(std::memory_order_acquire)) break;

        // Check execution limits (instruction budget, deadline, cancellation).
        if (!ctx.tick()) {
            // Attempt to cancel the in-flight request.
            uv_cancel(reinterpret_cast<uv_req_t*>(&req.req));
            // Drain the loop so the cancellation/completion callback fires
            // before the FsRequest goes out of scope.
            drain();
            return false;
        }

        // Avoid busy-spinning — yield briefly.
        // 50us is negligible compared to disk I/O latency (50us-10ms SSD).
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    return true;
}

bool UvSession::await_work(etil::core::ExecutionContext& ctx,
                            WorkRequest& work_req,
                            std::atomic<bool>* cancel_flag) {
    while (!work_req.done.load(std::memory_order_acquire)) {
        uv_run(&loop_, UV_RUN_NOWAIT);

        if (work_req.done.load(std::memory_order_acquire)) break;

        if (!ctx.tick()) {
            // Signal the work function to abort (it checks this flag).
            if (cancel_flag) {
                cancel_flag->store(true, std::memory_order_relaxed);
            }
            // Try to cancel if work hasn't started yet on the thread pool.
            uv_cancel(reinterpret_cast<uv_req_t*>(&work_req.req));
            // Must wait for completion — work_req lives on caller's stack.
            // The work function's own timeout (e.g. HTTP 10s) bounds this.
            while (!work_req.done.load(std::memory_order_acquire)) {
                uv_run(&loop_, UV_RUN_NOWAIT);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return false;
        }

        // Network RTT is 10ms-1s; 500us is a good polling interval.
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    return true;
}

void UvSession::drain() {
    // Run the loop until all pending callbacks have fired.
    // UV_RUN_DEFAULT blocks until there are no more active handles/requests.
    uv_run(&loop_, UV_RUN_DEFAULT);
}

} // namespace etil::fileio
