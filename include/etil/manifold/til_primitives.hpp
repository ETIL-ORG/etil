#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Manifold TIL-word registration. See doc B §21 and the Phase 2 task
/// list in docs/claude-design/20260418C-Manifold-Phase-0-1-2-Implementation-Plan.md.

namespace etil::core { class Dictionary; }

namespace etil::manifold {

/// Register all channel-* / role-*-channel / channel-perm-* / channel-origin
/// / channel-seq / channel-*-stats words. Called from
/// register_primitives() in src/core/primitives.cpp.
void register_manifold_primitives(etil::core::Dictionary& dict);

} // namespace etil::manifold
