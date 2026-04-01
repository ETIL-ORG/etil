// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/observable_async.hpp"
#include "etil/core/observable_execution.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/fileio/uv_session.hpp"

#include <chrono>
#include <thread>

namespace etil::core {

bool run_async_pipeline(ExecutionContext& ctx, AsyncPipeline& pipeline) {
    auto* uv = ctx.uv_session();
    if (!uv) return false;
    auto* loop = uv->loop();

    pipeline.register_handles(loop);

    bool ok = true;
    while (!pipeline.complete()) {
        uv_run(loop, UV_RUN_NOWAIT);
        pipeline.check_deferred();
        if (!ctx.tick()) {
            ok = false;
            pipeline.stop_all();
            break;
        }
        if (pipeline.complete()) break;
        // Check stopped only if no deferred groups are pending
        if (pipeline.stopped() && !pipeline.has_pending_deferred()) break;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    pipeline.close_all();
    // Drain pending close callbacks
    uv_run(loop, UV_RUN_NOWAIT);
    return ok;
}

} // namespace etil::core
