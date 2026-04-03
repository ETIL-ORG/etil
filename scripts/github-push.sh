#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Push master and tags to GitHub (origin).
#
# Usage: github-push.sh [--dry-run]
#
# Run this AFTER super-push.sh has merged, tagged, and pushed to the CI server,
# and AFTER the CI build has passed. This is the final step in the feature branch
# workflow — it publishes to the public GitHub repository.
#
# Pre-flight checks:
#   - Must be on master
#   - Must have an 'origin' remote
#   - Local master must match the CI remote (no divergence)

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
            echo "Push master and tags to GitHub (origin)."
            echo "Run after super-push.sh and CI pass."
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
    etil_die "Must be on master (currently on '$BRANCH')"
fi

if ! git remote get-url origin >/dev/null 2>&1; then
    etil_die "No 'origin' remote configured"
fi

# --- Show what will be pushed ---
etil_parse_version
LATEST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "(none)")
ORIGIN_URL=$(git remote get-url origin)

etil_log "Version:    v$ETIL_VERSION"
etil_log "Latest tag: $LATEST_TAG"
etil_log "Remote:     $ORIGIN_URL"

# Show commits not yet on origin
UNPUSHED=$(git log origin/master..master --oneline 2>/dev/null || echo "(unable to compare — origin/master may not exist yet)")
if [[ -n "$UNPUSHED" ]]; then
    etil_log "Commits to push:"
    echo "$UNPUSHED" | while IFS= read -r line; do
        echo "  $line"
    done
else
    etil_log "No new commits to push"
fi

if [[ "$DRY_RUN" == true ]]; then
    etil_log "[dry-run] Would run: git push origin master --tags"
    exit 0
fi

# --- Push ---
git push origin master --tags
etil_log "Pushed master + tags to GitHub"
