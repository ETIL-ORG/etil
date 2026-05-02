#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

// Process-global wall-clock timeout for asynchronous pipeline polls and
// channel-subscription drains. Read at the start of each polling loop and
// used to bound how long the loop will wait on I/O before returning.
//
// The TIL surface is `pipeline-wait-timeout! ( seconds-f -- )` —
// floating-point seconds, 0.0 (or negative) means no timeout, default 30.0.

#include <chrono>

namespace etil::core {

void   set_pipeline_wait_timeout_seconds(double seconds);
double get_pipeline_wait_timeout_seconds();

// Returns now + timeout, or time_point::max() when the timeout is 0/negative.
std::chrono::steady_clock::time_point compute_pipeline_deadline();

} // namespace etil::core
