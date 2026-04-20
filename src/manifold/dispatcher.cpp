// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/dispatcher.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "etil/core/logging.hpp"
#include "route_state.hpp"

namespace etil::manifold {

namespace {

auto& log() {
    static auto l = etil::core::logging::get("etil.manifold");
    return l;
}

// ---------------------------------------------------------------------------
// ThreadDispatcher
// ---------------------------------------------------------------------------

/// Internal state shared between the dispatcher outer and its worker
/// thread. Held via shared_ptr so the worker can safely out-live the
/// outer when shutdown() has to detach (plan §8.2).
struct TdState {
    // --- queue + run control ---
    std::mutex                 mu;
    std::condition_variable    cv;          ///< signals "item available or stopping"
    std::condition_variable    drained;     ///< signals "queue empty AND not in flight"
    std::queue<DeliveryItem>   q;
    bool                       stop = false;
    std::atomic<bool>          in_flight{false};

    // --- stats ---
    std::atomic<uint64_t>      delivered{0};
    std::atomic<uint64_t>      exceptions{0};
    std::atomic<uint64_t>      idle_transitions{0};

    // --- worker-exit notification (used by shutdown's bounded join) ---
    std::mutex                 done_mu;
    std::condition_variable    done_cv;
    bool                       worker_done = false;

    // --- Phase 5a.6 pause hook (test-only) ---
    // Protected by `mu` above — NOT by a separate mutex. Keeping it
    // under the same lock as `q` and `stop` means test_pause() is
    // observed atomically with the worker's wait predicate: the
    // worker CANNOT wake to pop an item while paused is true, even
    // if it was already in cv.wait() when the pause flag was set.
    bool                       paused = false;

    // --- user-supplied delivery callback ---
    DeliveryFn                 deliver;
};

/// Loop body — runs on the dedicated worker thread. Takes `st` by
/// value (shared_ptr copy) so the worker keeps state alive after the
/// outer dispatcher has been destroyed.
void td_run(std::shared_ptr<TdState> st) {
    while (true) {
        DeliveryItem item;
        {
            std::unique_lock<std::mutex> lk(st->mu);
            // Phase 5a.6 — pause is part of the wait predicate. While
            // `paused` is true the worker stays in cv.wait() even if
            // the queue is non-empty; tests can observe queue-depth
            // grow without the worker sneaking an item through. stop
            // still takes priority so shutdown always makes progress.
            st->cv.wait(lk, [&] {
                return st->stop || (!st->paused && !st->q.empty());
            });
            if (st->q.empty()) break;   // stop_ must be true per predicate
            item = std::move(st->q.front());
            st->q.pop();
            st->in_flight.store(true, std::memory_order_release);
        }

        // Invariant I5: sink exceptions are contained here. The
        // publisher thread never sees them.
        try {
            if (st->deliver) st->deliver(item.state, item.msg);
        } catch (const std::exception& e) {
            st->exceptions.fetch_add(1, std::memory_order_relaxed);
            log()->warn("dispatcher caught exception during delivery on "
                        "channel {}: {}", item.msg.channel, e.what());
        } catch (...) {
            st->exceptions.fetch_add(1, std::memory_order_relaxed);
            log()->warn("dispatcher caught unknown exception during "
                        "delivery on channel {}", item.msg.channel);
        }

        st->delivered.fetch_add(1, std::memory_order_relaxed);
        st->in_flight.store(false, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lk(st->mu);
            if (st->q.empty()) {
                st->idle_transitions.fetch_add(1, std::memory_order_relaxed);
            }
        }
        st->drained.notify_all();
    }

    // Notify anyone waiting on the bounded-join path.
    {
        std::lock_guard<std::mutex> lk(st->done_mu);
        st->worker_done = true;
    }
    st->done_cv.notify_all();
}

class ThreadDispatcher : public IDispatcher {
public:
    explicit ThreadDispatcher(DeliveryFn deliver)
        : st_(std::make_shared<TdState>()) {
        st_->deliver = std::move(deliver);
        worker_ = std::thread(td_run, st_);
    }

    ~ThreadDispatcher() override {
        shutdown();
    }

    ThreadDispatcher(const ThreadDispatcher&)            = delete;
    ThreadDispatcher& operator=(const ThreadDispatcher&) = delete;

    void enqueue(DeliveryItem item) override {
        {
            std::lock_guard<std::mutex> lk(st_->mu);
            st_->q.push(std::move(item));
        }
        st_->cv.notify_one();
    }

    void flush() override {
        std::unique_lock<std::mutex> lk(st_->mu);
        st_->drained.wait(lk, [&] {
            return st_->q.empty() &&
                   !st_->in_flight.load(std::memory_order_acquire);
        });
    }

    /// Bounded shutdown. Sets stop_, notifies the worker so it drains
    /// the queue, then waits up to kTimeoutMs for the worker to
    /// finish. On timeout the worker is detached — it continues
    /// running on its own shared_ptr<TdState> copy and cleans up
    /// naturally whenever the stuck sink returns.
    void shutdown() override {
        if (shutdown_called_) return;
        shutdown_called_ = true;

        {
            std::lock_guard<std::mutex> lk(st_->mu);
            st_->stop = true;
        }
        st_->cv.notify_all();

        if (!worker_.joinable()) {
            return;
        }

        using namespace std::chrono;
        bool finished;
        {
            std::unique_lock<std::mutex> lk(st_->done_mu);
            finished = st_->done_cv.wait_for(
                lk, milliseconds(kShutdownTimeoutMs),
                [&] { return st_->worker_done; });
        }

        if (finished) {
            worker_.join();
        } else {
            log()->warn("ThreadDispatcher shutdown timed out after {}ms; "
                        "detaching worker thread (stuck sink). Worker owns "
                        "its own shared_ptr<TdState> copy so the detach is "
                        "memory-safe; it will exit naturally when the sink "
                        "releases.",
                        kShutdownTimeoutMs);
            worker_.detach();
        }
    }

    DispatcherStats stats() const override {
        DispatcherStats s;
        {
            std::lock_guard<std::mutex> lk(st_->mu);
            s.queue_depth = st_->q.size();
        }
        s.delivered_count       = st_->delivered.load(std::memory_order_acquire);
        s.dispatcher_exceptions = st_->exceptions.load(std::memory_order_acquire);
        s.idle_transitions      = st_->idle_transitions.load(std::memory_order_acquire);
        return s;
    }

    void test_pause() override {
        std::lock_guard<std::mutex> lk(st_->mu);
        st_->paused = true;
    }

    void test_resume() override {
        {
            std::lock_guard<std::mutex> lk(st_->mu);
            st_->paused = false;
        }
        st_->cv.notify_all();
    }

private:
    static constexpr int kShutdownTimeoutMs = 1000;

    std::shared_ptr<TdState> st_;
    std::thread              worker_;
    bool                     shutdown_called_ = false;
};

// ---------------------------------------------------------------------------
// InlineDispatcher — synchronous test dispatcher
// ---------------------------------------------------------------------------

/// Runs delivery on the caller's thread inside enqueue(). flush() and
/// shutdown() are no-ops. Useful for tests that want deterministic
/// ordering without needing to reason about flush fences.
class InlineDispatcher : public IDispatcher {
public:
    explicit InlineDispatcher(DeliveryFn deliver)
        : deliver_(std::move(deliver)) {}

    void enqueue(DeliveryItem item) override {
        try {
            if (deliver_) deliver_(item.state, item.msg);
        } catch (const std::exception& e) {
            exceptions_.fetch_add(1, std::memory_order_relaxed);
            log()->warn("inline dispatcher caught exception: {}", e.what());
        } catch (...) {
            exceptions_.fetch_add(1, std::memory_order_relaxed);
        }
        delivered_.fetch_add(1, std::memory_order_relaxed);
    }

    void flush() override {}
    void shutdown() override {}

    DispatcherStats stats() const override {
        DispatcherStats s;
        s.queue_depth           = 0;
        s.delivered_count       = delivered_.load(std::memory_order_acquire);
        s.dispatcher_exceptions = exceptions_.load(std::memory_order_acquire);
        s.idle_transitions      = 0;
        return s;
    }

private:
    DeliveryFn            deliver_;
    std::atomic<uint64_t> delivered_{0};
    std::atomic<uint64_t> exceptions_{0};
};

} // namespace

std::unique_ptr<IDispatcher> make_thread_dispatcher(DeliveryFn deliver) {
    return std::make_unique<ThreadDispatcher>(std::move(deliver));
}

std::unique_ptr<IDispatcher> make_inline_dispatcher(DeliveryFn deliver) {
    return std::make_unique<InlineDispatcher>(std::move(deliver));
}

} // namespace etil::manifold
