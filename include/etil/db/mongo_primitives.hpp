#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


namespace etil::core { class Dictionary; }

namespace etil::db {

/// Register MongoDB TIL primitives in the dictionary.
///
/// Filter/doc args accept String, Json, or Map. Options accept String, Json, or Map.
///   mongo-find     ( coll filter opts -- result-json flag )
///   mongo-count    ( coll filter opts -- count flag )
///   mongo-insert   ( coll doc -- inserted-id flag )
///   mongo-update   ( coll filter update opts -- count flag )
///   mongo-delete   ( coll filter opts -- count flag )
///
/// All gated by RolePermissions::mongo_access.
/// All results are tainted (external data).
void register_mongo_primitives(etil::core::Dictionary& dict);

} // namespace etil::db
