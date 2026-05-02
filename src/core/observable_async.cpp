// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/observable_async.hpp"
#include "etil/core/observable_execution.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/pipeline_wait.hpp"
#include "etil/fileio/uv_session.hpp"

#include <chrono>
#include <thread>

namespace etil::core {

bool run_async_pipeline(ExecutionContext& ctx, AsyncPipeline& pipeline) {
    auto* uv = ctx.uv_session();
    if (!uv) return false;
    auto* loop = uv->loop();

    pipeline.register_handles(loop);

    // Wall-clock bound for purely-idle waits. compute_pipeline_deadline()
    // returns time_point::max() when the pipeline-wait-timeout is 0/disabled,
    // which collapses the check below to a no-op.
    const auto deadline = compute_pipeline_deadline();

    bool ok = true;
    while (!pipeline.complete()) {
        uv_run(loop, UV_RUN_NOWAIT);
        pipeline.check_deferred();
        // Charge ctx.tick() only on iters where an observer callback actually
        // fired. Purely-idle iters (waiting on libuv) consume no budget — the
        // wall-clock deadline below is what bounds them. This preserves the
        // runaway-user-code defense while letting genuine I/O waits proceed.
        if (pipeline.consume_deliveries_flag()) {
            if (!ctx.tick()) {
                ok = false;
                pipeline.stop_all();
                break;
            }
        }
        if (pipeline.complete()) break;
        // Check stopped only if no deferred groups are pending
        if (pipeline.stopped() && !pipeline.has_pending_deferred()) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            ok = false;
            pipeline.stop_all();
            break;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    pipeline.close_all();
    // Drain pending close callbacks
    uv_run(loop, UV_RUN_NOWAIT);
    return ok;
}

} // namespace etil::core
