#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Bump the project version in CMakeLists.txt using semver semantics.
#
# Usage: version-bump.sh <major|minor|patch> [--dry-run]
#
# Modes:
#   major   — X+1.0.0  (breaking changes)
#   minor   — X.Y+1.0  (new feature, patch resets to 0)
#   patch   — X.Y.Z+1  (bug fix / phase increment)
#
# Echoes the new version string on success.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

# --- Parse args ---
MODE=""
DRY_RUN=false

for arg in "$@"; do
    case "$arg" in
        major|minor|patch) MODE="$arg" ;;
        --dry-run)         DRY_RUN=true ;;
        --help|-h)
            echo "Usage: $0 <major|minor|patch> [--dry-run]"
            echo ""
            echo "  major     X+1.0.0  (breaking changes)"
            echo "  minor     X.Y+1.0  (new feature, patch resets to 0)"
            echo "  patch     X.Y.Z+1  (bug fix / phase increment)"
            echo "  --dry-run Show plan without modifying CMakeLists.txt"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Run with --help for usage."
            exit 1
            ;;
    esac
done

if [[ -z "$MODE" ]]; then
    etil_die "Mode required: major, minor, or patch"
fi

# --- Compute new version ---
etil_parse_version
OLD_VERSION="$ETIL_VERSION"

case "$MODE" in
    major)
        NEW_VERSION="$((ETIL_VERSION_MAJOR + 1)).0.0"
        ;;
    minor)
        NEW_VERSION="${ETIL_VERSION_MAJOR}.$((ETIL_VERSION_MINOR + 1)).0"
        ;;
    patch)
        NEW_VERSION="${ETIL_VERSION_MAJOR}.${ETIL_VERSION_MINOR}.$((ETIL_VERSION_PATCH + 1))"
        ;;
esac

if [[ "$DRY_RUN" == true ]]; then
    etil_log "[dry-run] Would bump v$OLD_VERSION → v$NEW_VERSION ($MODE)"
    echo "$NEW_VERSION"
    exit 0
fi

# --- Apply to CMakeLists.txt ---
CMAKE_FILE="$ETIL_PROJECT_DIR/CMakeLists.txt"
sed -i "s/project(EvolutionaryTIL VERSION ${OLD_VERSION}/project(EvolutionaryTIL VERSION ${NEW_VERSION}/" "$CMAKE_FILE"

# --- Verify ---
local_verify=$(grep -oP 'project\s*\(\s*EvolutionaryTIL\s+VERSION\s+\K[0-9]+\.[0-9]+\.[0-9]+' "$CMAKE_FILE")
if [[ "$local_verify" != "$NEW_VERSION" ]]; then
    etil_die "Version bump failed: expected $NEW_VERSION, got $local_verify"
fi

# --- Update cached values ---
ETIL_VERSION="$NEW_VERSION"
case "$MODE" in
    major)
        ETIL_VERSION_MAJOR="$((ETIL_VERSION_MAJOR + 1))"
        ETIL_VERSION_MINOR=0
        ETIL_VERSION_PATCH=0
        ;;
    minor)
        ETIL_VERSION_MINOR="$((ETIL_VERSION_MINOR + 1))"
        ETIL_VERSION_PATCH=0
        ;;
    patch)
        ETIL_VERSION_PATCH="$((ETIL_VERSION_PATCH + 1))"
        ;;
esac

etil_log "Bumped v$OLD_VERSION → v$NEW_VERSION ($MODE)"
echo "$NEW_VERSION"
