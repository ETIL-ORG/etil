// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/evolve_logger.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <typeindex>

#include "etil/manifold/message.hpp"
#include "etil/manifold/service.hpp"

namespace etil::evolution {

namespace {

void publish_category(etil::manifold::ChannelService* svc,
                       EvolveLogCategory cat,
                       const char* level,
                       const std::string& msg) {
    if (!svc) return;
    etil::manifold::Message m;
    m.channel = std::string("etil.evolution.") +
                 EvolveLogger::category_channel_suffix(cat);
    m.payload = msg;
    m.payload_type = std::type_index(typeid(std::string));
    m.tags["level"] = level;
    svc->publish(std::move(m));
}

} // namespace

EvolveLogger::~EvolveLogger() {
    stop();
}

void EvolveLogger::start(EvolveLogLevel level, uint32_t category_mask) {
    stop();  // close any existing file
    level_ = level;
    categories_ = category_mask;
    if (level_ == EvolveLogLevel::Off) return;

    std::string path = make_filename();
    file_.open(path, std::ios::out | std::ios::trunc);
    if (file_.is_open()) {
        file_ << timestamp() << " [evolve] Log started — level="
              << (level_ == EvolveLogLevel::Logical ? "logical" : "granular")
              << " mask=0x" << std::hex << categories_ << std::dec
              << "\n";
        file_.flush();
    }
}

void EvolveLogger::stop() {
    if (file_.is_open()) {
        file_ << timestamp() << " [evolve] Log stopped\n";
        file_.flush();
        file_.close();
    }
    level_ = EvolveLogLevel::Off;
}

void EvolveLogger::set_directory(const std::string& dir) {
    directory_ = dir;
    // Ensure trailing slash
    if (!directory_.empty() && directory_.back() != '/') {
        directory_ += '/';
    }
}

void EvolveLogger::log(EvolveLogCategory cat, const std::string& msg) {
    if (!enabled(cat)) return;
    if (file_.is_open()) {
        file_ << timestamp() << " [" << category_tag(cat) << "] " << msg << "\n";
        file_.flush();
    }
    publish_category(channels_, cat, "logical", msg);
}

void EvolveLogger::detail(EvolveLogCategory cat, const std::string& msg) {
    if (!granular(cat)) return;
    if (file_.is_open()) {
        file_ << timestamp() << " [" << category_tag(cat) << ":detail] " << msg << "\n";
        file_.flush();
    }
    publish_category(channels_, cat, "granular", msg);
}

std::string EvolveLogger::timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    struct tm tm_buf{};
    localtime_r(&time_t, &tm_buf);

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string EvolveLogger::make_filename() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    struct tm tm_buf{};
    localtime_r(&time_t, &tm_buf);

    std::ostringstream oss;
    oss << directory_
        << std::put_time(&tm_buf, "%Y%m%dT%H%M%S")
        << "-evolve.log";
    return oss.str();
}

const char* EvolveLogger::category_tag(EvolveLogCategory cat) {
    switch (cat) {
        case EvolveLogCategory::Engine:      return "engine";
        case EvolveLogCategory::Decompile:   return "decompile";
        case EvolveLogCategory::Substitute:  return "substitute";
        case EvolveLogCategory::Perturb:     return "perturb";
        case EvolveLogCategory::Move:        return "move";
        case EvolveLogCategory::ControlFlow: return "control";
        case EvolveLogCategory::Grow:        return "grow";
        case EvolveLogCategory::Shrink:      return "shrink";
        case EvolveLogCategory::Crossover:   return "crossover";
        case EvolveLogCategory::Repair:      return "repair";
        case EvolveLogCategory::Compile:     return "compile";
        case EvolveLogCategory::Fitness:     return "fitness";
        case EvolveLogCategory::Selection:   return "selection";
        case EvolveLogCategory::Bridge:      return "bridge";
        case EvolveLogCategory::Diff:        return "diff";
        case EvolveLogCategory::ASTDump:     return "ast-dump";
        default:                             return "unknown";
    }
}

const char* EvolveLogger::category_channel_suffix(EvolveLogCategory cat) {
    // Mirror category_tag but use dot-safe segments (the mapping is
    // 1:1; dashes in the tags table stay as dashes in the channel
    // subsegment, which is legal per channel-name grammar).
    switch (cat) {
        case EvolveLogCategory::Engine:      return "engine";
        case EvolveLogCategory::Decompile:   return "decompile";
        case EvolveLogCategory::Substitute:  return "substitute";
        case EvolveLogCategory::Perturb:     return "perturb";
        case EvolveLogCategory::Move:        return "move";
        case EvolveLogCategory::ControlFlow: return "control";
        case EvolveLogCategory::Grow:        return "grow";
        case EvolveLogCategory::Shrink:      return "shrink";
        case EvolveLogCategory::Crossover:   return "crossover";
        case EvolveLogCategory::Repair:      return "repair";
        case EvolveLogCategory::Compile:     return "compile";
        case EvolveLogCategory::Fitness:     return "fitness";
        case EvolveLogCategory::Selection:   return "selection";
        case EvolveLogCategory::Bridge:      return "bridge";
        case EvolveLogCategory::Diff:        return "diff";
        case EvolveLogCategory::ASTDump:     return "ast-dump";
        case EvolveLogCategory::DAG:         return "dag";
        default:                             return "unknown";
    }
}

} // namespace etil::evolution
