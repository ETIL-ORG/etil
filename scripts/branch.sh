#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Create a feature branch with a version bump.
#
# Usage: branch.sh [--dry-run]
#   --dry-run   Show plan without executing
#
# Creates a branch named YYYYMMDDThhmmss-vX.Y.Z with a version bump commit.
# Must be run from master with a clean working tree.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

etil_require_cmd git

# --- Parse args ---
DRY_RUN=false

for arg in "$@"; do
    case "$arg" in
        --dry-run)  DRY_RUN=true ;;
        --help|-h)
            echo "Usage: $0 [--dry-run]"
            echo ""
            echo "Creates a feature branch with a version bump commit."
            echo "Branch name: YYYYMMDDThhmmss-vX.Y.Z (new version)"
            echo ""
            echo "  --dry-run   Show plan without executing"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Run with --help for usage."
            exit 1
            ;;
    esac
done

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

# --- Compute new version ---
etil_parse_version
OLD_VERSION="$ETIL_VERSION"
NEW_PATCH=$((ETIL_VERSION_PATCH + 1))
NEW_VERSION="${ETIL_VERSION_MAJOR}.${ETIL_VERSION_MINOR}.${NEW_PATCH}"

# --- Branch name ---
TIMESTAMP=$(date -u '+%Y%m%dT%H%M%S')
BRANCH_NAME="${TIMESTAMP}-v${NEW_VERSION}"

etil_log "Current version: v$OLD_VERSION"
etil_log "New version:     v$NEW_VERSION"
etil_log "Branch name:     $BRANCH_NAME"

if [[ "$DRY_RUN" == true ]]; then
    etil_log "[dry-run] Would create branch '$BRANCH_NAME'"
    etil_log "[dry-run] Would bump CMakeLists.txt: v$OLD_VERSION → v$NEW_VERSION"
    etil_log "[dry-run] Would commit: Bump version to v$NEW_VERSION"
    exit 0
fi

# --- Create branch ---
git checkout -b "$BRANCH_NAME"
etil_log "Created branch: $BRANCH_NAME"

# --- Bump version ---
etil_version_bump_patch >/dev/null
etil_parse_version
etil_log "Bumped CMakeLists.txt: v$OLD_VERSION → v$ETIL_VERSION"

# --- Commit ---
git add CMakeLists.txt
git commit -m "$(cat <<EOF
Bump version to v$ETIL_VERSION

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"

etil_log "Feature branch ready: $BRANCH_NAME"
etil_log "Make your changes, then run: super-push.sh --message \"your message\""
