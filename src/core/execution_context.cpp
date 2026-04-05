// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/execution_context.hpp"

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
