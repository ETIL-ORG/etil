#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Super Push: build → test → bump → commit → tag → push.
# Supports both master and feature branches (created by branch.sh).
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
            echo "On master: build → test → bump → commit → tag → push"
            echo "On feature branch: build → test → commit → merge to master → resolve version → tag → push"
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

detect_change_type() {
    # Collect all changed files: committed since last tag + staged + unstaged + untracked
    local last_tag changed_committed changed_staged changed_unstaged changed_untracked changed_files
    last_tag=$(git describe --tags --abbrev=0 2>/dev/null || echo "")
    changed_committed=""
    if [[ -n "$last_tag" ]]; then
        changed_committed=$(git diff --name-only "$last_tag"..HEAD 2>/dev/null || true)
    fi
    changed_staged=$(git diff --cached --name-only 2>/dev/null || true)
    changed_unstaged=$(git diff --name-only 2>/dev/null || true)
    changed_untracked=$(git ls-files --others --exclude-standard 2>/dev/null || true)
    changed_files=$(printf '%s\n%s\n%s\n%s' "$changed_committed" "$changed_staged" "$changed_unstaged" "$changed_untracked" | sort -u)

    # Docs-only patterns: README, CLAUDE.md, *.md in docs/
    DOCS_ONLY=true
    while IFS= read -r file; do
        [[ -z "$file" ]] && continue
        case "$file" in
            README.md|CLAUDE.md|docs/*|*.md) ;;
            *) DOCS_ONLY=false; break ;;
        esac
    done <<< "$changed_files"

    if [[ "$DOCS_ONLY" == true ]]; then
        etil_log "Change type: documentation only (no version bump)"
    else
        etil_log "Change type: code change"
    fi
}

resolve_version_conflict() {
    # After merging a feature branch to master, check for version conflicts.
    # If master's version >= branch's version, bump master's version.
    local branch_version="$1"

    etil_parse_version
    local master_version="$ETIL_VERSION"

    # Compare branch version vs current master version
    local cmp_result=0
    etil_compare_versions "$branch_version" "$master_version" || cmp_result=$?

    case $cmp_result in
        0)
            # Equal — no conflict
            etil_log "Version v$branch_version — no conflict"
            ;;
        1)
            # Branch > Master — branch is ahead, no conflict
            etil_log "Version v$branch_version > master v$master_version — no conflict"
            ;;
        2)
            # Branch <= Master — conflict: another branch was merged first
            etil_log "Version conflict: branch had v$branch_version, master at v$master_version"
            local new_version
            new_version=$(etil_version_bump_patch)
            etil_parse_version
            etil_log "Resolved: bumped to v$ETIL_VERSION"

            # Amend the merge commit to include the version fix
            git add CMakeLists.txt
            git commit --amend --no-edit
            ;;
    esac
}

# --- Step 1: Pre-flight checks ---
step "Pre-flight checks"

cd "$ETIL_PROJECT_DIR"

BRANCH=$(git rev-parse --abbrev-ref HEAD)
ON_MASTER=false
if [[ "$BRANCH" == "master" ]]; then
    ON_MASTER=true
fi
etil_log "Branch: $BRANCH"

if [[ "$ON_MASTER" == true ]]; then
    # On master: must have changes to commit
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
detect_change_type

ETIL_GIT_REMOTE="${ETIL_GIT_REMOTE:-origin}"

if [[ "$ON_MASTER" == true ]]; then
    # ========================================
    # MASTER WORKFLOW (existing behavior)
    # ========================================

    # Stage everything
    if [[ "$DRY_RUN" == false ]]; then
        git add -A
    fi

    # --- Version bump (if code change) ---
    if [[ "$DOCS_ONLY" == false ]]; then
        step "Version bump"
        if [[ "$DRY_RUN" == true ]]; then
            local_new="${ETIL_VERSION_MAJOR}.${ETIL_VERSION_MINOR}.$((ETIL_VERSION_PATCH + 1))"
            etil_log "[dry-run] Would bump v$ETIL_VERSION → v$local_new"
        else
            NEW_VERSION=$(etil_version_bump_patch)
            etil_parse_version
            etil_log "Bumped CMakeLists.txt: v$ETIL_VERSION"
            git add -A
        fi
    fi

    # --- Commit ---
    step "Commit"
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

    # --- Tag ---
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

    # --- Push ---
    step "Push"
    run git push "$ETIL_GIT_REMOTE" master --tags

else
    # ========================================
    # FEATURE BRANCH WORKFLOW
    # ========================================
    FEATURE_BRANCH="$BRANCH"

    # Save the branch version (set by branch.sh)
    BRANCH_VERSION="$ETIL_VERSION"

    # --- Commit changes on the branch (if any) ---
    step "Commit on branch"

    HAS_CHANGES=false
    if ! git diff --quiet 2>/dev/null; then
        HAS_CHANGES=true
    elif ! git diff --cached --quiet 2>/dev/null; then
        HAS_CHANGES=true
    elif [[ -n "$(git ls-files --others --exclude-standard)" ]]; then
        HAS_CHANGES=true
    fi

    if [[ "$HAS_CHANGES" == true ]]; then
        if [[ "$DRY_RUN" == true ]]; then
            etil_log "[dry-run] git add -A && git commit on $FEATURE_BRANCH"
        else
            git add -A
            git commit -m "$(cat <<EOF
$MESSAGE

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
            )"
            etil_log "Committed on $FEATURE_BRANCH"
        fi
    else
        etil_log "No uncommitted changes on branch"
    fi

    # --- Switch to master and pull latest ---
    step "Update master"
    if [[ "$DRY_RUN" == true ]]; then
        etil_log "[dry-run] git checkout master && git pull $ETIL_GIT_REMOTE master"
    else
        git checkout master
        git pull "$ETIL_GIT_REMOTE" master || true
    fi

    # --- Merge feature branch ---
    step "Merge $FEATURE_BRANCH → master"
    if [[ "$DRY_RUN" == true ]]; then
        etil_log "[dry-run] git merge $FEATURE_BRANCH --no-ff -m \"Merge $FEATURE_BRANCH: $MESSAGE\""
    else
        if ! git merge "$FEATURE_BRANCH" --no-ff -m "$(cat <<EOF
Merge $FEATURE_BRANCH: $MESSAGE

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
        )"; then
            etil_log "MERGE CONFLICT — aborting merge"
            git merge --abort
            git checkout "$FEATURE_BRANCH"
            etil_die "Merge conflicts detected. Resolve manually, then retry."
        fi
        etil_log "Merged $FEATURE_BRANCH into master"
    fi

    # --- Resolve version conflicts ---
    step "Version conflict resolution"
    if [[ "$DRY_RUN" == true ]]; then
        etil_log "[dry-run] Would check branch v$BRANCH_VERSION against master"
    else
        resolve_version_conflict "$BRANCH_VERSION"
    fi

    # Re-parse final version
    etil_parse_version

    # --- Tag ---
    step "Tag"
    if [[ "$DRY_RUN" == true ]]; then
        etil_log "[dry-run] git tag -a v$BRANCH_VERSION"
    else
        git tag -a "v$ETIL_VERSION" -m "v$ETIL_VERSION: $MESSAGE"
        etil_log "Tagged v$ETIL_VERSION"
    fi

    # --- Push ---
    step "Push"
    run git push "$ETIL_GIT_REMOTE" master --tags

    # --- Clean up feature branch ---
    step "Cleanup"
    if [[ "$DRY_RUN" == true ]]; then
        etil_log "[dry-run] git branch -d $FEATURE_BRANCH"
    else
        git branch -d "$FEATURE_BRANCH"
        etil_log "Deleted local branch: $FEATURE_BRANCH"
    fi
fi

# --- Summary ---
step "Super push complete"
etil_log "Version: v$ETIL_VERSION"
etil_log "Message: $MESSAGE"
etil_log "CI will build, test, and deploy on the production server"
