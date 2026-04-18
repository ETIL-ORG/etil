// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/subject.hpp"

#include <chrono>
#include <utility>

#include "etil/manifold/service.hpp"
#include "etil/manifold/sink.hpp"

namespace etil::manifold {

ChannelSubject::~ChannelSubject() {
    // Close first so anyone waiting wakes up; then drop the route.
    close();
    if (auto svc = cleanup_svc_.lock()) {
        if (cleanup_handle_.valid()) {
            svc->remove_route(cleanup_handle_);
        }
    }
}

void ChannelSubject::push(Message msg) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_) return;
        queue_.push_back(std::move(msg));
    }
    cv_.notify_all();
}

bool ChannelSubject::pop_wait(Message& msg, int wait_ms) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait_for(lock, std::chrono::milliseconds(wait_ms),
                 [this] { return !queue_.empty() || closed_; });
    if (!queue_.empty()) {
        msg = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }
    return false;
}

void ChannelSubject::close() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        closed_ = true;
    }
    cv_.notify_all();
}

bool ChannelSubject::is_closed() const {
    std::lock_guard<std::mutex> lock(mu_);
    return closed_;
}

size_t ChannelSubject::queue_size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
}

void ChannelSubject::set_cleanup(std::weak_ptr<ChannelService> svc,
                                 RouteHandle handle) {
    cleanup_svc_ = std::move(svc);
    cleanup_handle_ = handle;
}

namespace {

class SubjectSink : public ISink {
public:
    explicit SubjectSink(std::shared_ptr<ChannelSubject> subject)
        : subject_(std::move(subject)) {}

    void accept(const Message& msg) override {
        subject_->push(msg);
    }

private:
    std::shared_ptr<ChannelSubject> subject_;
};

} // namespace

std::shared_ptr<ISink> make_subject_sink(std::shared_ptr<ChannelSubject> subject) {
    return std::make_shared<SubjectSink>(std::move(subject));
}

} // namespace etil::manifold
