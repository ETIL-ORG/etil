#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/dictionary.hpp"

namespace etil::core {

class ExecutionContext;

void register_json_primitives(Dictionary& dict);

bool prim_json_parse(ExecutionContext& ctx);
bool prim_json_dump(ExecutionContext& ctx);
bool prim_json_pretty(ExecutionContext& ctx);
bool prim_json_get(ExecutionContext& ctx);
bool prim_json_length(ExecutionContext& ctx);
bool prim_json_type(ExecutionContext& ctx);
bool prim_json_keys(ExecutionContext& ctx);
bool prim_json_to_map(ExecutionContext& ctx);
bool prim_json_to_array(ExecutionContext& ctx);
bool prim_map_to_json(ExecutionContext& ctx);
bool prim_array_to_json(ExecutionContext& ctx);
bool prim_json_to_value(ExecutionContext& ctx);

} // namespace etil::core
