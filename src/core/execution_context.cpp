// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/execution_context.hpp"

#ifdef __x86_64__
#include <cpuid.h>
#endif

namespace etil::core {

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
