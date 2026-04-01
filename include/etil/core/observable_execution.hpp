#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_observable.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/value_helpers.hpp"

#include <functional>

namespace etil::core {

/// Observer callback: receives a value, returns true to continue, false to stop.
using Observer = std::function<bool(Value, ExecutionContext&)>;

class AsyncPipeline;  // defined in observable_async.cpp

/// Unified pipeline execution — single entry point for sync and async.
///
/// When pipeline is non-null, operators register libuv handles for async
/// execution and return true (nodes registered, not yet executed).
/// When null, operators execute synchronously (blocking).
/// Transforms use shared_ptr state, so the observer wrapping is identical
/// in both modes. Only sources and combination operators branch on the
/// pipeline pointer.
bool execute_pipeline(HeapObservable* obs, ExecutionContext& ctx,
                      const Observer& observer,
                      AsyncPipeline* pipeline = nullptr);

/// Legacy synchronous execution for non-migrated operators
/// (Last, Buffer, BufferWhen, Window, Finalize, Catch, File I/O, HTTP).
bool execute_observable(HeapObservable* obs, ExecutionContext& ctx,
                        const Observer& observer);

/// Sleep until target time or until tick() fails (budget/deadline/cancel).
bool sleep_until_or_tick(ExecutionContext& ctx,
                         std::chrono::steady_clock::time_point target);

/// Execute an Xt that should return an observable, pop and validate the result.
HeapObservable* execute_xt_pop_observable(WordImpl* xt, ExecutionContext& ctx,
                                          const char* caller = nullptr);

} // namespace etil::core
