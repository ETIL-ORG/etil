// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/execution_context.hpp"

#include "etil/core/execution_output_tee.hpp"

#ifndef ETIL_WASM_BUILD
#include "etil/fileio/uv_session.hpp"
#endif

#ifdef __x86_64__
#include <cpuid.h>
#endif

namespace etil::core {

ExecutionContext::ExecutionContext(uint32_t thread_id)
    : thread_id_(thread_id)
    , data_field_registry_(std::make_shared<DataFieldRegistry>())
    , start_time_(std::chrono::high_resolution_clock::now())
{
}

ExecutionContext::~ExecutionContext() = default;

// --- Phase 4a: stdout tee management ---------------------------------------
//
// When a ChannelService is attached, interpose a tee streambuf on
// out_ so writes are published onto etil.repl.stdout in addition
// to being forwarded to the underlying sink. Detaching the service
// (set_channels(nullptr)) restores the original out_.

void ExecutionContext::set_channels(etil::manifold::ChannelService* c) {
    channels_ = c;
    if (c) rebuild_stdout_tee();
    else   clear_stdout_tee();
}

void ExecutionContext::set_session_id(std::string id) {
    session_id_ = std::move(id);
    if (channels_) rebuild_stdout_tee();
}

void ExecutionContext::set_out(std::ostream* s) {
    // Clear any existing tee — the caller is installing a new
    // downstream. Update out_original_ so rebuild_stdout_tee can
    // wrap the new downstream if channels are active.
    if (out_tee_) {
        out_tee_.reset();
    }
    out_original_ = s;
    out_ = s;
    if (channels_) rebuild_stdout_tee();
}

void ExecutionContext::rebuild_stdout_tee() {
    if (!channels_) {
        clear_stdout_tee();
        return;
    }
    // Lazily capture the current downstream (before our tee gets
    // installed). If out_original_ is null we haven't teed yet;
    // out_ points at the real downstream.
    std::ostream* downstream = out_original_ ? out_original_ : out_;
    if (!downstream) return;
    out_original_ = downstream;
    out_tee_ = std::make_unique<ExecutionOutputTee>(
        downstream->rdbuf(), channels_, session_id_, "etil.repl.stdout");
    out_ = out_tee_->stream.get();
}

void ExecutionContext::clear_stdout_tee() {
    if (!out_tee_) return;
    // Flush any pending line before tearing the tee down.
    if (out_tee_->stream) out_tee_->stream->flush();
    out_tee_.reset();
    if (out_original_) out_ = out_original_;
}

#ifndef ETIL_WASM_BUILD
etil::fileio::UvSession* ExecutionContext::uv_session() {
    if (uv_session_external_) return uv_session_external_;
    if (!uv_session_owned_) {
        uv_session_owned_ = std::make_unique<etil::fileio::UvSession>();
    }
    return uv_session_owned_.get();
}
#else
etil::fileio::UvSession* ExecutionContext::uv_session() {
    return nullptr;  // no libuv in WASM builds
}
#endif

SIMDContext::SIMDContext() {
#ifdef __x86_64__
    // Check for AVX2 and AVX-512 support
    unsigned int eax, ebx, ecx, edx;
    
    // Check AVX2
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        avx2_available = (ebx & (1 << 5)) != 0;
        avx512_available = (ebx & (1 << 16)) != 0;
    }
#elif defined(__ARM_NEON)
    neon_available = true;
#endif
}

GPUContext::GPUContext() {
    // TODO: Implement CUDA/OpenCL detection
    cuda_available = false;
    device_id = -1;
}

} // namespace etil::core
