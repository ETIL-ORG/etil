#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/heap_object.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace etil::core {

/// Immutable, reference-counted JSON container.
///
/// Wraps a parsed nlohmann::json value. No child Value refs — nlohmann::json
/// owns its own memory tree. Default destructor suffices.
class HeapJson final : public HeapObject {
public:
    explicit HeapJson(nlohmann::json j)
        : HeapObject(Kind::Json), json_(std::move(j)) {}

    const nlohmann::json& json() const { return json_; }
    std::string dump() const { return json_.dump(); }
    std::string dump(int indent) const { return json_.dump(indent); }

private:
    nlohmann::json json_;
};

} // namespace etil::core
