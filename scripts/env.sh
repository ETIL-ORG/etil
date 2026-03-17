#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Common environment for ETIL scripts.
# Sourced (not executed) by all other scripts in this directory.
#
# Provides:
#   - Auto-detected paths (ETIL_SCRIPTS_DIR, ETIL_PROJECT_DIR, etc.)
#   - Version parsing from CMakeLists.txt
#   - SSH and Docker constants
#   - CMake flags matching current build configuration
#   - Helper functions (etil_log, etil_die, etil_parse_version, etc.)

# Guard against double-sourcing
if [[ -n "${_ETIL_ENV_LOADED:-}" ]]; then
    return 0 2>/dev/null || true
fi
_ETIL_ENV_LOADED=1

# --- Paths (auto-detected from this file's location) ---
ETIL_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ETIL_PROJECT_DIR="$(cd "$ETIL_SCRIPTS_DIR/.." && pwd)"
ETIL_WORKSPACE_DIR="$(cd "$ETIL_PROJECT_DIR/.." && pwd)"
ETIL_BUILD_DEBUG_DIR="$ETIL_WORKSPACE_DIR/build-debug"
ETIL_BUILD_RELEASE_DIR="$ETIL_WORKSPACE_DIR/build"

# --- Pre-built dependencies ---
ETIL_DEPS_PREFIX="${ETIL_DEPS_PREFIX:-$ETIL_WORKSPACE_DIR/lib}"
ETIL_DEPS_DEBUG_DIR="$ETIL_DEPS_PREFIX/debug"
ETIL_DEPS_RELEASE_DIR="$ETIL_DEPS_PREFIX/release"

# --- SSH (configure for your deployment target) ---
ETIL_SSH_HOST="${ETIL_SSH_HOST:-deploy@your-server.example.com}"

# --- Docker ---
ETIL_IMAGE_NAME="etil-mcp"
ETIL_CONTAINER_NAME="etil-mcp-http"

# --- CMake flags (match current build configuration) ---
ETIL_CMAKE_COMMON_FLAGS="-DETIL_BUILD_HTTP_CLIENT=ON -DETIL_BUILD_JWT=ON -DETIL_BUILD_MONGODB=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

# --- Helper functions ---

etil_log() {
    echo "[etil] $(date '+%H:%M:%S') $*" >&2
}

etil_die() {
    etil_log "FATAL: $*"
    exit 1
}

etil_require_cmd() {
    local cmd="$1"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        etil_die "Required command not found: $cmd"
    fi
}

etil_parse_version() {
    # Parse version from CMakeLists.txt → set ETIL_VERSION_* vars
    local cmake_file="$ETIL_PROJECT_DIR/CMakeLists.txt"
    if [[ ! -f "$cmake_file" ]]; then
        etil_die "CMakeLists.txt not found: $cmake_file"
    fi

    ETIL_VERSION=$(grep -oP 'project\s*\(\s*EvolutionaryTIL\s+VERSION\s+\K[0-9]+\.[0-9]+\.[0-9]+' "$cmake_file")
    if [[ -z "$ETIL_VERSION" ]]; then
        etil_die "Could not parse version from $cmake_file"
    fi

    ETIL_VERSION_MAJOR="${ETIL_VERSION%%.*}"
    local rest="${ETIL_VERSION#*.}"
    ETIL_VERSION_MINOR="${rest%%.*}"
    ETIL_VERSION_PATCH="${rest#*.}"
}

etil_version_bump_patch() {
    # Increment patch number in CMakeLists.txt, echo new version
    etil_parse_version
    local new_patch=$((ETIL_VERSION_PATCH + 1))
    local old_version="$ETIL_VERSION"
    local new_version="${ETIL_VERSION_MAJOR}.${ETIL_VERSION_MINOR}.${new_patch}"
    local cmake_file="$ETIL_PROJECT_DIR/CMakeLists.txt"

    sed -i "s/project(EvolutionaryTIL VERSION ${old_version}/project(EvolutionaryTIL VERSION ${new_version}/" "$cmake_file"

    # Verify the change took effect
    local verify
    verify=$(grep -oP 'project\s*\(\s*EvolutionaryTIL\s+VERSION\s+\K[0-9]+\.[0-9]+\.[0-9]+' "$cmake_file")
    if [[ "$verify" != "$new_version" ]]; then
        etil_die "Version bump failed: expected $new_version, got $verify"
    fi

    # Update cached values
    ETIL_VERSION="$new_version"
    ETIL_VERSION_PATCH="$new_patch"

    echo "$new_version"
}

etil_compare_versions() {
    # Compare two semver strings: v1 vs v2
    # Returns: 0=equal, 1=v1>v2, 2=v1<v2
    local v1="$1" v2="$2"

    local v1_major="${v1%%.*}"
    local v1_rest="${v1#*.}"
    local v1_minor="${v1_rest%%.*}"
    local v1_patch="${v1_rest#*.}"

    local v2_major="${v2%%.*}"
    local v2_rest="${v2#*.}"
    local v2_minor="${v2_rest%%.*}"
    local v2_patch="${v2_rest#*.}"

    if (( v1_major > v2_major )); then return 1; fi
    if (( v1_major < v2_major )); then return 2; fi
    if (( v1_minor > v2_minor )); then return 1; fi
    if (( v1_minor < v2_minor )); then return 2; fi
    if (( v1_patch > v2_patch )); then return 1; fi
    if (( v1_patch < v2_patch )); then return 2; fi
    return 0
}

# --- Parse version on source ---
etil_parse_version
