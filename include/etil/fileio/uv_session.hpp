#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <atomic>
#include <cstdint>

#include <uv.h>

namespace etil::core { class ExecutionContext; }

namespace etil::fileio {

/// RAII wrapper around uv_fs_t with completion tracking.
///
/// The callback sets `done` to true (release ordering); the await loop
/// reads it with acquire ordering.  The destructor calls uv_fs_req_cleanup()
/// to free any libuv-allocated buffers.
struct FsRequest {
    uv_fs_t req{};
    std::atomic<bool> done{false};
    ssize_t result = 0;

    FsRequest() { req.data = this; }
    ~FsRequest() { uv_fs_req_cleanup(&req); }

    FsRequest(const FsRequest&) = delete;
    FsRequest& operator=(const FsRequest&) = delete;

    /// Standard libuv callback: sets done flag and copies result.
    static void on_complete(uv_fs_t* r) {
        auto* self = static_cast<FsRequest*>(r->data);
        self->result = r->result;
        self->done.store(true, std::memory_order_release);
    }

    /// Reset for reuse (must only be called when no async op is in flight).
    void reset() {
        uv_fs_req_cleanup(&req);
        req = uv_fs_t{};
        req.data = this;
        done.store(false, std::memory_order_relaxed);
        result = 0;
    }
};

/// RAII wrapper around uv_work_t with completion tracking.
///
/// Used for running blocking operations (e.g. HTTP requests) on libuv's
/// thread pool with cooperative await via tick().
struct WorkRequest {
    uv_work_t req{};
    std::atomic<bool> done{false};
    void* user_data = nullptr;  // Opaque pointer for work function data

    WorkRequest() { req.data = this; }
    ~WorkRequest() = default;

    WorkRequest(const WorkRequest&) = delete;
    WorkRequest& operator=(const WorkRequest&) = delete;

    /// libuv after-work callback: sets done flag on event loop thread.
    static void on_after_work(uv_work_t* r, int /*status*/) {
        auto* self = static_cast<WorkRequest*>(r->data);
        self->done.store(true, std::memory_order_release);
    }
};

/// Per-session libuv event loop wrapper.
///
/// Owns a uv_loop_t that runs on the interpreter thread.  Async file
/// operations use libuv's thread pool for the actual I/O; the completion
/// callback runs on the loop thread (= interpreter thread) so it can
/// safely set FsRequest::done without touching interpreter state.
///
/// The await_completion() method spin-polls uv_run(UV_RUN_NOWAIT) with
/// ctx.tick() checks, preserving execution limit enforcement during I/O.
class UvSession {
public:
    UvSession();
    ~UvSession();

    UvSession(const UvSession&) = delete;
    UvSession& operator=(const UvSession&) = delete;

    /// The underlying event loop (for uv_fs_* calls).
    uv_loop_t* loop() { return &loop_; }

    /// Block until req.done becomes true, polling the event loop with
    /// UV_RUN_NOWAIT between tick() checks.
    ///
    /// Returns true if the request completed normally.
    /// Returns false if an execution limit was hit (cancellation, timeout,
    /// instruction budget).  On false, the caller must still clean up
    /// any open file descriptors — the request itself has been cancelled
    /// or allowed to complete via drain.
    bool await_completion(etil::core::ExecutionContext& ctx, FsRequest& req);

    /// Block until work_req.done becomes true, polling the event loop.
    ///
    /// On execution limit hit, sets *cancel_flag (if non-null) then waits
    /// for the work to complete (it must have its own timeout).
    /// Returns true on normal completion, false on cancellation.
    bool await_work(etil::core::ExecutionContext& ctx, WorkRequest& work_req,
                    std::atomic<bool>* cancel_flag = nullptr);

private:
    uv_loop_t loop_{};

    /// After cancellation, drain the loop so all pending callbacks fire
    /// before FsRequest objects go out of scope.
    void drain();
};

} // namespace etil::fileio
