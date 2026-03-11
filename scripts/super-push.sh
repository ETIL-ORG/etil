#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Super Push: build → test → bump → commit → tag → push.
# Deployment is handled by CI (triggered by post-receive hook).
#
# Usage: super-push.sh --message "commit message" [--dry-run]
#   --message   Commit message (required)
#   --dry-run   Show plan without executing

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

etil_require_cmd git
etil_require_cmd ninja
etil_require_cmd cmake
etil_require_cmd ctest

# --- Parse args ---
MESSAGE=""
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --message|-m)
            if [[ -z "${2:-}" ]]; then
                echo "Error: --message requires an argument"
                exit 1
            fi
            MESSAGE="$2"; shift 2 ;;
        --dry-run)    DRY_RUN=true; shift ;;
        --help|-h)
            echo "Usage: $0 --message \"commit message\" [--dry-run]"
            echo ""
            echo "  --message   Commit message (required)"
            echo "  --dry-run   Show plan without executing"
            echo ""
            echo "Deployment is handled by CI after push."
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Run with --help for usage."
            exit 1
            ;;
    esac
done

if [[ -z "$MESSAGE" ]]; then
    etil_die "--message is required"
fi

# --- Helpers ---
run() {
    if [[ "$DRY_RUN" == true ]]; then
        etil_log "[dry-run] $*"
    else
        "$@"
    fi
}

step() {
    etil_log "=== $1 ==="
}

# --- Step 1: Pre-flight checks ---
step "Pre-flight checks"

cd "$ETIL_PROJECT_DIR"

# Must be on master branch
BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [[ "$BRANCH" != "master" ]]; then
    etil_die "Not on master branch (currently on '$BRANCH')"
fi
etil_log "Branch: $BRANCH"

# Must have changes to commit (modified, staged, or untracked)
HAS_CHANGES=false
if ! git diff --quiet 2>/dev/null; then
    HAS_CHANGES=true
elif ! git diff --cached --quiet 2>/dev/null; then
    HAS_CHANGES=true
elif [[ -n "$(git ls-files --others --exclude-standard)" ]]; then
    HAS_CHANGES=true
fi

if [[ "$HAS_CHANGES" == false ]]; then
    etil_die "No changes to commit."
fi

etil_log "Version: v$ETIL_VERSION"

# --- Step 2: Build all ---
step "Build all"
run "$SCRIPT_DIR/build.sh" all

# --- Step 3: Test all ---
step "Test all"
run "$SCRIPT_DIR/test.sh" all

# --- Step 4: Detect change type ---
step "Detect change type"

# Stage everything so we can inspect what will be committed
if [[ "$DRY_RUN" == false ]]; then
    git add -A
fi

# Collect all changed files: committed since last tag + staged + unstaged + untracked
LAST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "")
CHANGED_COMMITTED=""
if [[ -n "$LAST_TAG" ]]; then
    CHANGED_COMMITTED=$(git diff --name-only "$LAST_TAG"..HEAD 2>/dev/null || true)
fi
CHANGED_STAGED=$(git diff --cached --name-only 2>/dev/null || true)
CHANGED_UNSTAGED=$(git diff --name-only 2>/dev/null || true)
CHANGED_UNTRACKED=$(git ls-files --others --exclude-standard 2>/dev/null || true)
CHANGED_FILES=$(printf '%s\n%s\n%s\n%s' "$CHANGED_COMMITTED" "$CHANGED_STAGED" "$CHANGED_UNSTAGED" "$CHANGED_UNTRACKED" | sort -u)

# Docs-only patterns: README, CLAUDE.md, *.md in docs/
DOCS_ONLY=true
while IFS= read -r file; do
    [[ -z "$file" ]] && continue
    case "$file" in
        README.md|CLAUDE.md|docs/*|*.md) ;;
        *) DOCS_ONLY=false; break ;;
    esac
done <<< "$CHANGED_FILES"

if [[ "$DOCS_ONLY" == true ]]; then
    etil_log "Change type: documentation only (no version bump)"
else
    etil_log "Change type: code change (will bump patch)"
fi

# --- Step 5: Version bump (if code change) ---

if [[ "$DOCS_ONLY" == false ]]; then
    step "Version bump"
    if [[ "$DRY_RUN" == true ]]; then
        local_new="${ETIL_VERSION_MAJOR}.${ETIL_VERSION_MINOR}.$((ETIL_VERSION_PATCH + 1))"
        etil_log "[dry-run] Would bump v$ETIL_VERSION → v$local_new"
    else
        NEW_VERSION=$(etil_version_bump_patch)
        # Re-parse in parent shell (subshell can't update our vars)
        etil_parse_version
        etil_log "Bumped CMakeLists.txt: v$ETIL_VERSION"

        # Re-stage after version bump
        git add -A
    fi
fi

# --- Step 6: Commit ---
step "Commit"

cd "$ETIL_PROJECT_DIR"

if [[ "$DRY_RUN" == true ]]; then
    etil_log "[dry-run] git commit"
    etil_log "[dry-run] Message: $MESSAGE"
else
    git commit -m "$(cat <<EOF
$MESSAGE

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
    )"
    etil_log "Committed"
fi

# --- Step 7: Tag ---
step "Tag"
if [[ "$DRY_RUN" == true ]]; then
    if [[ "$DOCS_ONLY" == false ]]; then
        etil_log "[dry-run] git tag -a v${ETIL_VERSION_MAJOR}.${ETIL_VERSION_MINOR}.$((ETIL_VERSION_PATCH + 1))"
    else
        etil_log "[dry-run] git tag -a v$ETIL_VERSION"
    fi
else
    git tag -a "v$ETIL_VERSION" -m "v$ETIL_VERSION: $MESSAGE"
    etil_log "Tagged v$ETIL_VERSION"
fi

# --- Step 8: Push ---
step "Push"
ETIL_GIT_REMOTE="${ETIL_GIT_REMOTE:-origin}"
run git push "$ETIL_GIT_REMOTE" master --tags

# --- Summary ---
step "Super push complete"
etil_log "Version: v$ETIL_VERSION"
etil_log "Message: $MESSAGE"
etil_log "CI will build, test, and deploy on the production server"
