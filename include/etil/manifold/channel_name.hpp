#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Channel name grammar and pattern matching.
///
/// Channel names are dotted segments: etil.mcp.out.notification.system.
/// Patterns add two wildcards:
///   *  — match exactly one segment
///   ** — match any depth (zero or more segments)
///
/// Per A-1 resolution (2026-04-18): version segments, when present, are
/// plain integers. `etil.foo.bar.2` is a legal versioned channel;
/// `etil.foo.bar.2.0.0` is not. Wildcards match version segments as
/// normal segments.

#include <string>
#include <string_view>

namespace etil::manifold {

/// True if `name` is a syntactically valid channel name (non-empty
/// dotted segments, alphanumeric / underscore / hyphen in each segment,
/// no leading/trailing/consecutive dots). Wildcards (`*`, `**`) are
/// allowed for pattern arguments but rejected for a concrete channel
/// name publishing context — use `validate_pattern` for patterns.
bool validate_channel_name(std::string_view name);

/// True if `pattern` is a syntactically valid pattern (validate_channel_name
/// rules + allowing `*` and `**` as a whole segment).
bool validate_pattern(std::string_view pattern);

/// True if `channel` matches `pattern` under the `*` / `**` wildcard
/// grammar. `*` matches one segment; `**` matches zero or more
/// segments. An empty pattern matches nothing.
bool channel_matches(std::string_view pattern, std::string_view channel);

/// Specificity score for a pattern — higher = more specific. Used for
/// RBAC grant ranking (doc B §15.3 step 5). Literal segments count
/// more than wildcards; `**` is the least specific.
int pattern_specificity(std::string_view pattern);

} // namespace etil::manifold
