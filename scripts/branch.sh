#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Create a feature branch with a minor version bump.
#
# Usage: branch.sh <branch-name> [--dry-run]
#   <branch-name>  Descriptive name (e.g., type-directed-bridges)
#   --dry-run      Show plan without executing
#
# Creates a branch with a minor version bump (X.Y+1.0) and initial commit.
# Must be run from master with a clean working tree.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

etil_require_cmd git

# --- Parse args ---
BRANCH_NAME=""
DRY_RUN=false

for arg in "$@"; do
    case "$arg" in
        --dry-run)  DRY_RUN=true ;;
        --help|-h)
            echo "Usage: $0 <branch-name> [--dry-run]"
            echo ""
            echo "Creates a feature branch with a minor version bump (X.Y+1.0)."
            echo "Branch name should be descriptive (e.g., type-directed-bridges)."
            echo ""
            echo "  --dry-run   Show plan without executing"
            exit 0
            ;;
        -*)
            echo "Unknown option: $arg"
            echo "Run with --help for usage."
            exit 1
            ;;
        *)
            if [[ -z "$BRANCH_NAME" ]]; then
                BRANCH_NAME="$arg"
            else
                echo "Unexpected argument: $arg"
                echo "Run with --help for usage."
                exit 1
            fi
            ;;
    esac
done

if [[ -z "$BRANCH_NAME" ]]; then
    etil_die "Branch name required. Usage: $0 <branch-name> [--dry-run]"
fi

cd "$ETIL_PROJECT_DIR"

# --- Pre-flight checks ---
BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [[ "$BRANCH" != "master" ]]; then
    etil_die "Must be on master to create a feature branch (currently on '$BRANCH')"
fi

if ! git diff --quiet 2>/dev/null || ! git diff --cached --quiet 2>/dev/null; then
    etil_die "Working tree is not clean. Commit or stash changes first."
fi

if [[ -n "$(git ls-files --others --exclude-standard)" ]]; then
    etil_die "Untracked files present. Commit or remove them first."
fi

# --- Preview or execute ---
etil_parse_version
OLD_VERSION="$ETIL_VERSION"

if [[ "$DRY_RUN" == true ]]; then
    NEW_VERSION=$("$SCRIPT_DIR/version-bump.sh" minor --dry-run)
    etil_log "[dry-run] Would create branch '$BRANCH_NAME'"
    etil_log "[dry-run] Would bump CMakeLists.txt: v$OLD_VERSION → v$NEW_VERSION (minor)"
    etil_log "[dry-run] Would commit: Bump version to v$NEW_VERSION"
    exit 0
fi

etil_log "Current version: v$OLD_VERSION"
etil_log "Branch name:     $BRANCH_NAME"

# --- Create branch ---
git checkout -b "$BRANCH_NAME"
etil_log "Created branch: $BRANCH_NAME"

# --- Bump minor version (resets patch to 0) ---
NEW_VERSION=$("$SCRIPT_DIR/version-bump.sh" minor)
etil_log "Bumped CMakeLists.txt: v$OLD_VERSION → v$NEW_VERSION (minor)"

# --- Commit ---
git add CMakeLists.txt
git commit -m "$(cat <<EOF
Bump version to v$NEW_VERSION

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"

etil_log "Feature branch ready: $BRANCH_NAME"
etil_log "Make your changes, then run: super-push.sh --message \"your message\""
