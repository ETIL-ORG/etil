#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Private header for async observable infrastructure.
// NOT a public API — lives in src/core/, not include/.

#include "etil/core/heap_object.hpp"

#include <functional>
#include <memory>
#include <vector>

#include <uv.h>

namespace etil::core {

class ExecutionContext;

/// Base class for async handle nodes in the pipeline.
struct AsyncNode {
    bool done = false;
    bool stopped = false;
    bool is_source = true;  // only source nodes affect completion tracking
    virtual ~AsyncNode() = default;
    virtual void register_on(uv_loop_t* loop) = 0;
    virtual void stop() = 0;
    virtual void close() = 0;
};

/// Observer callback: receives a value, returns true to continue, false to stop.
using Observer = std::function<bool(Value, ExecutionContext&)>;

/// Timer source node: wraps a uv_timer_t.
struct TimerNode : AsyncNode {
    uv_timer_t handle{};
    int64_t counter = 0;
    uint64_t delay_ms = 0;
    uint64_t period_ms = 0;
    Observer observer;
    ExecutionContext* ctx = nullptr;

    static void on_timer(uv_timer_t* h) {
        auto* self = static_cast<TimerNode*>(h->data);
        if (self->stopped) return;
        if (!self->observer(Value(self->counter++), *self->ctx)) {
            self->stopped = true;
            self->done = true;
            uv_timer_stop(h);
            return;
        }
        if (self->period_ms == 0) {
            self->done = true;
            uv_timer_stop(h);
        }
    }

    void register_on(uv_loop_t* loop) override {
        handle.data = this;
        uv_timer_init(loop, &handle);
        uv_timer_start(&handle, on_timer, delay_ms, period_ms);
    }

    void stop() override {
        stopped = true;
        uv_timer_stop(&handle);
        done = true;
    }

    static void on_close(uv_handle_t*) {}
    void close() override {
        uv_close(reinterpret_cast<uv_handle_t*>(&handle), on_close);
    }
};

/// Idle source node: emits pre-collected values one per loop iteration.
struct IdleNode : AsyncNode {
    uv_idle_t handle{};
    std::vector<Value> values;
    size_t index = 0;
    Observer observer;
    ExecutionContext* ctx = nullptr;

    static void on_idle(uv_idle_t* h) {
        auto* self = static_cast<IdleNode*>(h->data);
        if (self->stopped || self->index >= self->values.size()) {
            self->done = true;
            uv_idle_stop(h);
            return;
        }
        if (!self->observer(self->values[self->index++], *self->ctx)) {
            self->stopped = true;
            uv_idle_stop(h);
            // Release unconsumed values
            for (size_t i = self->index; i < self->values.size(); ++i) {
                value_release(self->values[i]);
            }
            self->values.clear();
            self->done = true;
        }
        if (self->index >= self->values.size()) {
            self->done = true;
            uv_idle_stop(h);
        }
    }

    void register_on(uv_loop_t* loop) override {
        handle.data = this;
        uv_idle_init(loop, &handle);
        uv_idle_start(&handle, on_idle);
    }

    void stop() override {
        stopped = true;
        uv_idle_stop(&handle);
        for (size_t i = index; i < values.size(); ++i) {
            value_release(values[i]);
        }
        values.clear();
        done = true;
    }

    static void on_close(uv_handle_t*) {}
    void close() override {
        uv_close(reinterpret_cast<uv_handle_t*>(&handle), on_close);
    }
};

/// Timer node for temporal transforms (not a source — doesn't affect completion).
/// The timer handle is managed by the pipeline; the observer callback controls it.
struct TransformTimerNode : AsyncNode {
    uv_timer_t handle{};

    TransformTimerNode() { is_source = false; done = true; /* never blocks completion */ }

    void register_on(uv_loop_t* loop) override {
        handle.data = this;
        uv_timer_init(loop, &handle);
        // Not started here — the observer callback starts/stops it as needed.
    }

    void stop() override {
        uv_timer_stop(&handle);
    }

    static void on_close(uv_handle_t*) {}
    void close() override {
        uv_timer_stop(&handle);
        uv_close(reinterpret_cast<uv_handle_t*>(&handle), on_close);
    }
};

/// Manages all async nodes for a pipeline execution.
class AsyncPipeline {
public:
    /// Add a node to the pipeline.
    void add_node(std::unique_ptr<AsyncNode> node) {
        nodes_.push_back(std::move(node));
    }

    /// Current number of nodes.
    size_t node_count() const { return nodes_.size(); }

    /// Extract all nodes (moves ownership out).
    std::vector<std::unique_ptr<AsyncNode>> extract_nodes() {
        return std::move(nodes_);
    }

    /// Access a node by index (non-owning).
    AsyncNode* node_at(size_t i) const { return nodes_[i].get(); }

    /// Add a deferred group — nodes that activate when condition() returns true.
    /// The condition checks source nodes added before this group.
    void add_deferred(std::vector<std::unique_ptr<AsyncNode>> nodes,
                      std::function<bool()> condition) {
        deferred_.push_back({std::move(nodes), std::move(condition)});
    }

    /// Register all active nodes on the event loop.
    void register_handles(uv_loop_t* loop) {
        loop_ = loop;
        registered_count_ = nodes_.size();
        for (auto& node : nodes_) {
            node->register_on(loop);
        }
    }

    /// Register any nodes added after initial registration (dynamic nodes).
    void register_new_nodes() {
        if (!loop_) return;
        while (registered_count_ < nodes_.size()) {
            nodes_[registered_count_]->register_on(loop_);
            ++registered_count_;
        }
    }

    /// Stop and close all nodes from index 'from' onward (for SwitchMap cancellation).
    void stop_and_close_from(size_t from) {
        for (size_t i = from; i < nodes_.size(); ++i) {
            nodes_[i]->stop();
            nodes_[i]->close();
        }
        // Drain close callbacks
        if (loop_) uv_run(loop_, UV_RUN_NOWAIT);
        nodes_.erase(nodes_.begin() + static_cast<long>(from), nodes_.end());
        if (registered_count_ > nodes_.size()) registered_count_ = nodes_.size();
    }

    /// Check deferred groups — activate any whose conditions are met.
    void check_deferred() {
        if (!loop_ || deferred_.empty()) return;
        for (auto& group : deferred_) {
            if (group.activated || group.nodes.empty()) continue;
            bool cond = group.condition();
            if (cond) {
                group.activated = true;
                for (auto& node : group.nodes) {
                    node->register_on(loop_);
                    nodes_.push_back(std::move(node));
                }
                group.nodes.clear();
            }
        }
    }

    /// Stop all active nodes (early termination).
    void stop_all() {
        for (auto& node : nodes_) {
            if (!node->done) node->stop();
        }
        // Deferred nodes that were never activated have no handles — just clear.
        for (auto& group : deferred_) {
            group.nodes.clear();
        }
    }

    /// Close all handles (must be called before loop destruction).
    void close_all() {
        for (auto& node : nodes_) {
            node->close();
        }
        // Deferred nodes that were never activated were never registered,
        // so they have no libuv handles to close. Just delete them.
        for (auto& group : deferred_) {
            group.nodes.clear();
        }
    }

    /// True when all source nodes have completed and no deferred groups pending.
    bool complete() const {
        for (const auto& node : nodes_) {
            if (node->is_source && !node->done) return false;
        }
        for (const auto& group : deferred_) {
            if (!group.activated) return false;
        }
        return true;
    }

    /// True when any active (not-yet-done) node signaled stop.
    /// Nodes that are both stopped and done completed normally (e.g., Take
    /// stopping a timer after N items) and should not trigger early exit.
    bool stopped() const {
        for (const auto& node : nodes_) {
            if (node->stopped && !node->done) return true;
        }
        return false;
    }

    /// True if pipeline has any registered nodes or pending deferred groups.
    bool has_nodes() const {
        return !nodes_.empty() || has_pending_deferred();
    }

    bool has_pending_deferred() const {
        for (const auto& group : deferred_) {
            if (!group.activated && !group.nodes.empty()) return true;
        }
        return false;
    }

    /// Check if all source nodes in a range [start, end) are done.
    bool sources_done_in_range(size_t start, size_t end) const {
        for (size_t i = start; i < end && i < nodes_.size(); ++i) {
            if (nodes_[i]->is_source && !nodes_[i]->done) return false;
        }
        return true;
    }

private:
    struct DeferredGroup {
        std::vector<std::unique_ptr<AsyncNode>> nodes;
        std::function<bool()> condition;
        bool activated = false;
    };

    std::vector<std::unique_ptr<AsyncNode>> nodes_;
    std::vector<DeferredGroup> deferred_;
    uv_loop_t* loop_ = nullptr;
    size_t registered_count_ = 0;
};

/// Run an async pipeline on the libuv event loop.
/// Polls UV_RUN_NOWAIT cooperatively with ctx.tick().
bool run_async_pipeline(ExecutionContext& ctx, AsyncPipeline& pipeline);

} // namespace etil::core
