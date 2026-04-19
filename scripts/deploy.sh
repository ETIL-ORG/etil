#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Deploy ETIL MCP Docker image to production server.
#
# Two modes:
#   Remote (default): build locally, transfer via SSH, restart on server
#   Local (--local-deploy): build and restart on the same machine (for CI)
#
# Usage (from workspace/):
#   evolutionary-til/scripts/deploy.sh              # full build + deploy
#   evolutionary-til/scripts/deploy.sh --skip-build  # reuse existing local image
#   evolutionary-til/scripts/deploy.sh --local-deploy # build + restart locally (CI)
#   evolutionary-til/scripts/deploy.sh --dry-run     # show plan, don't execute
#
# Prerequisites (remote mode):
#   - Docker installed locally
#   - SSH connectivity to the production server (ETIL_SSH_HOST)
# Prerequisites (local-deploy mode):
#   - Docker installed locally
#   - Release binary at $ETIL_BUILD_RELEASE_DIR/bin/etil_mcp_server

set -euo pipefail

# --- Common environment ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

# --- Resolve compose command (plugin vs. legacy hyphenated binary) ---
# Prefer the `docker compose` plugin where present (Docker 20.10+); fall
# back to the legacy `docker-compose` v1 binary, which is what ships in
# older Ubuntu images and the current etil-ci container.
if docker compose version >/dev/null 2>&1; then
    DOCKER_COMPOSE="docker compose"
elif command -v docker-compose >/dev/null 2>&1; then
    DOCKER_COMPOSE="docker-compose"
else
    echo "ERROR: neither 'docker compose' plugin nor 'docker-compose' binary found" >&2
    exit 1
fi

# --- Derived constants ---
SSH_CMD="ssh $ETIL_SSH_HOST"
SCP_CMD="scp"
LOCAL_TARBALL="/tmp/etil-mcp-deploy.tar.gz"
REMOTE_TARBALL="/tmp/etil-mcp-deploy.tar.gz"
REMOTE_ENV_FILE="/tmp/etil-deploy-env"
ENV_FALLBACK="$ETIL_PROJECT_DIR/deploy/production/.env"
COMPOSE_FILE_LOCAL="$ETIL_PROJECT_DIR/deploy/production/docker-compose.prod.yml"
REMOTE_COMPOSE_FILE="/tmp/etil-docker-compose.prod.yml"
LOCAL_DEPLOY_ENV="/tmp/etil-compose-deploy.env"
REMOTE_DEPLOY_ENV="/tmp/etil-compose-deploy.env"
START_TIME=$SECONDS

# --- Parse args ---
SKIP_BUILD=false
DRY_RUN=false
LOCAL_DEPLOY=false

for arg in "$@"; do
    case "$arg" in
        --skip-build)   SKIP_BUILD=true ;;
        --dry-run)      DRY_RUN=true ;;
        --local-deploy) LOCAL_DEPLOY=true ;;
        --help|-h)
            echo "Usage: $0 [--skip-build] [--local-deploy] [--dry-run] [--help]"
            echo ""
            echo "  --skip-build    Reuse existing local Docker image (skip build step)"
            echo "  --local-deploy  Build and deploy on this machine (no SSH transfer)"
            echo "  --dry-run       Show what would be done without executing"
            echo "  --help          Show this help"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Run with --help for usage."
            exit 1
            ;;
    esac
done

# --- Version from env.sh ---
VERSION="$ETIL_VERSION"
DEPLOY_MODE="remote"
if [ "$LOCAL_DEPLOY" = true ]; then
    DEPLOY_MODE="local"
fi
echo "=== ETIL Deploy ($DEPLOY_MODE) ==="
echo "Version:      v$VERSION"
echo "Image:        $ETIL_IMAGE_NAME:$VERSION"
echo "Container:    $ETIL_CONTAINER_NAME"
echo "Skip build:   $SKIP_BUILD"
echo "Local deploy: $LOCAL_DEPLOY"
echo "Dry run:      $DRY_RUN"
echo ""

# --- Helper: run or print ---
run() {
    if [ "$DRY_RUN" = true ]; then
        echo "[dry-run] $*"
    else
        "$@"
    fi
}

run_ssh() {
    if [ "$DRY_RUN" = true ]; then
        echo "[dry-run] ssh production server: $1"
    else
        $SSH_CMD "$1"
    fi
}

# --- Cleanup trap ---
cleanup() {
    rm -f "$LOCAL_TARBALL"
    rm -rf "$ETIL_PROJECT_DIR/.docker-stage"
    if [ "$DRY_RUN" = false ] && [ "$LOCAL_DEPLOY" = false ]; then
        $SSH_CMD "rm -f $REMOTE_TARBALL $REMOTE_ENV_FILE $REMOTE_COMPOSE_FILE $REMOTE_DEPLOY_ENV" 2>/dev/null || true
    fi
    if [ "$LOCAL_DEPLOY" = true ]; then
        rm -f "$LOCAL_DEPLOY_ENV" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# --- Step 1: Verify prerequisites ---
echo "=== Verifying prerequisites ==="

if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: docker not found"
    exit 1
fi
echo "Docker:     OK"

if [ "$LOCAL_DEPLOY" = false ]; then
    if [ "$DRY_RUN" = false ]; then
        if ! $SSH_CMD "echo ok" >/dev/null 2>&1; then
            echo "ERROR: Cannot connect to $ETIL_SSH_HOST"
            exit 1
        fi
        echo "SSH access: OK"
    else
        echo "[dry-run] SSH access: skipped"
    fi
fi
echo ""

# --- Step 2: Build Docker image ---
if [ "$SKIP_BUILD" = true ]; then
    echo "=== Skipping build (--skip-build) ==="
    # Verify image exists locally
    if [ "$DRY_RUN" = false ]; then
        if ! docker image inspect "$ETIL_IMAGE_NAME:latest" >/dev/null 2>&1; then
            echo "ERROR: No local image $ETIL_IMAGE_NAME:latest — cannot skip build"
            exit 1
        fi
    fi
else
    # Rebuild release to ensure binary matches current source (version, etc.)
    echo "=== Rebuilding release binary ==="
    run ninja -C "$ETIL_BUILD_RELEASE_DIR"

    RELEASE_BINARY="$ETIL_BUILD_RELEASE_DIR/bin/etil_mcp_server"
    if [ ! -f "$RELEASE_BINARY" ]; then
        echo "ERROR: Release binary not found: $RELEASE_BINARY"
        echo "  Run 'scripts/build.sh release --configure' first."
        exit 1
    fi

    echo "=== Staging binary for Docker build ==="
    mkdir -p "$ETIL_PROJECT_DIR/.docker-stage/bin"
    cp "$RELEASE_BINARY" "$ETIL_PROJECT_DIR/.docker-stage/bin/"
    echo "Staged: $RELEASE_BINARY"

    echo "=== Building Docker image ==="
    run docker build \
        -t "$ETIL_IMAGE_NAME:$VERSION" \
        -t "$ETIL_IMAGE_NAME:latest" \
        "$ETIL_PROJECT_DIR"
fi
echo ""

# --- Step 3: Tag image (ensure both version + latest) ---
echo "=== Tagging image ==="
run docker tag "$ETIL_IMAGE_NAME:latest" "$ETIL_IMAGE_NAME:$VERSION"
run docker tag "$ETIL_IMAGE_NAME:$VERSION" "$ETIL_IMAGE_NAME:latest"
echo ""

if [ "$LOCAL_DEPLOY" = true ]; then
    # --- Local deploy: image is already on this host's Docker daemon ---
    echo "=== Local deploy: docker compose up -d ==="
    if [ "$DRY_RUN" = true ]; then
        echo "[dry-run] write $LOCAL_DEPLOY_ENV, docker compose -f $COMPOSE_FILE_LOCAL up -d"
    else
        # Build deploy env file from (in order of precedence):
        #   1. running container's existing ETIL_* env (preserves runtime state)
        #   2. on-disk fallback .env in deploy/production/.env
        #
        # Host-specific paths (oauth dir, mongo cert paths, container mount
        # points, etc.) live ONLY in the fallback .env — this script never
        # hardcodes them. Required vars are enumerated in .env.example.
        # `docker compose up -d` uses ${VAR:?} syntax and will fail loudly
        # if any required var is missing from the merged env file.
        rm -f "$LOCAL_DEPLOY_ENV"

        if docker inspect "$ETIL_CONTAINER_NAME" >/dev/null 2>&1; then
            docker inspect "$ETIL_CONTAINER_NAME" --format '{{range .Config.Env}}{{println .}}{{end}}' \
                | grep '^ETIL_' > "$LOCAL_DEPLOY_ENV" 2>/dev/null || true
            echo "Env vars extracted from running container"
        else
            touch "$LOCAL_DEPLOY_ENV"
        fi

        # Merge fallback .env entries for any keys the running container
        # didn't expose (paths, GIDs, etc.).
        if [ -f "$ENV_FALLBACK" ]; then
            while IFS='=' read -r key _val; do
                [ -z "$key" ] && continue
                case "$key" in \#*) continue ;; esac
                if ! grep -q "^${key}=" "$LOCAL_DEPLOY_ENV"; then
                    grep "^${key}=" "$ENV_FALLBACK" >> "$LOCAL_DEPLOY_ENV" || true
                fi
            done < <(grep '^ETIL_' "$ENV_FALLBACK" || true)
            echo "Env vars merged from $ENV_FALLBACK"
        fi

        chmod 600 "$LOCAL_DEPLOY_ENV"
        echo "Env file keys (values masked):"
        sed 's/=.*/=***/' "$LOCAL_DEPLOY_ENV"

        # Ensure external volumes + network exist before compose up —
        # everything marked `external: true` in the compose file must be
        # pre-created; compose won't manage their lifecycle.
        docker volume create etil-sessions 2>/dev/null || true
        docker volume create etil-library 2>/dev/null || true
        docker network create etil-net 2>/dev/null || true

        $DOCKER_COMPOSE -f "$COMPOSE_FILE_LOCAL" --env-file "$LOCAL_DEPLOY_ENV" up -d
        echo "$DOCKER_COMPOSE up -d completed"
    fi
    echo ""
else
    # --- Remote deploy: save, transfer, load on remote server ---

    # --- Step 4: Save + compress ---
    echo "=== Saving image to tarball ==="
    if [ "$DRY_RUN" = true ]; then
        echo "[dry-run] docker save $ETIL_IMAGE_NAME:$VERSION $ETIL_IMAGE_NAME:latest | gzip > $LOCAL_TARBALL"
    else
        docker save "$ETIL_IMAGE_NAME:$VERSION" "$ETIL_IMAGE_NAME:latest" | gzip > "$LOCAL_TARBALL"
        TARBALL_SIZE=$(du -h "$LOCAL_TARBALL" | cut -f1)
        echo "Tarball: $LOCAL_TARBALL ($TARBALL_SIZE)"
    fi
    echo ""

    # --- Step 5: Transfer to production server ---
    echo "=== Transferring to production server ==="
    if [ "$DRY_RUN" = true ]; then
        echo "[dry-run] scp $LOCAL_TARBALL production server:$REMOTE_TARBALL"
        echo "[dry-run] scp $COMPOSE_FILE_LOCAL production server:$REMOTE_COMPOSE_FILE"
    else
        $SCP_CMD "$LOCAL_TARBALL" "$ETIL_SSH_HOST:$REMOTE_TARBALL"
        $SCP_CMD "$COMPOSE_FILE_LOCAL" "$ETIL_SSH_HOST:$REMOTE_COMPOSE_FILE"
        echo "Transfer complete (image + compose file)"
    fi
    echo ""

    # --- Step 6: Extract env vars from running container ---
    echo "=== Extracting env vars from running container ==="
    if [ "$DRY_RUN" = true ]; then
        echo "[dry-run] Extract ETIL_* env vars from $ETIL_CONTAINER_NAME on production server"
    else
        ENV_EXTRACTED=$($SSH_CMD "bash -s" <<'EXTRACT_ENV'
set -euo pipefail
CONTAINER="etil-mcp-http"
ENV_FILE="/tmp/etil-deploy-env"

if docker inspect "$CONTAINER" >/dev/null 2>&1; then
    docker inspect "$CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}' \
        | grep '^ETIL_' > "$ENV_FILE" 2>/dev/null || true
    if [ -s "$ENV_FILE" ]; then
        echo "EXTRACTED"
        cat "$ENV_FILE"
    else
        echo "EMPTY"
    fi
else
    echo "NO_CONTAINER"
fi
EXTRACT_ENV
        )

        if echo "$ENV_EXTRACTED" | head -1 | grep -q "EXTRACTED"; then
            echo "Env vars extracted from running container:"
            echo "$ENV_EXTRACTED" | tail -n +2 | sed 's/=.*/=***/'
        elif echo "$ENV_EXTRACTED" | head -1 | grep -q "NO_CONTAINER"; then
            echo "No running container found — using fallback .env"
            if [ ! -f "$ENV_FALLBACK" ]; then
                echo "ERROR: No fallback .env at $ENV_FALLBACK"
                echo "  This appears to be a first deploy. Create .env from .env.example first."
                exit 1
            fi
            grep '^ETIL_' "$ENV_FALLBACK" | $SSH_CMD "cat > $REMOTE_ENV_FILE"
            echo "Uploaded env vars from $ENV_FALLBACK"
        else
            echo "WARNING: No ETIL_* env vars found. Container may start with defaults."
        fi
    fi
    echo ""

    # --- Step 7: Load image + restart container ---
    echo "=== Loading image and restarting container on production server ==="
    if [ "$DRY_RUN" = true ]; then
        echo "[dry-run] docker load + tag + stop + rm + run on production server"
    else
        $SSH_CMD "bash -s" <<REMOTE_DEPLOY
set -euo pipefail

VERSION="$VERSION"
IMAGE="$ETIL_IMAGE_NAME"
CONTAINER="$ETIL_CONTAINER_NAME"
ENV_FILE="$REMOTE_ENV_FILE"
TARBALL="$REMOTE_TARBALL"
COMPOSE_FILE="$REMOTE_COMPOSE_FILE"
DEPLOY_ENV_FILE="$REMOTE_DEPLOY_ENV"

echo "--- Loading image ---"
LOAD_OUTPUT=\$(docker load < "\$TARBALL" 2>&1)
echo "\$LOAD_OUTPUT"

IMAGE_ID=""
if echo "\$LOAD_OUTPUT" | grep -q "Loaded image ID:"; then
    IMAGE_ID=\$(echo "\$LOAD_OUTPUT" | grep "Loaded image ID:" | head -1 | awk '{print \$NF}')
elif echo "\$LOAD_OUTPUT" | grep -q "Loaded image:"; then
    IMAGE_ID=\$(echo "\$LOAD_OUTPUT" | grep "Loaded image:" | head -1 | awk '{print \$NF}')
fi

echo "--- Tagging image ---"
if [ -n "\$IMAGE_ID" ]; then
    docker tag "\$IMAGE_ID" "\$IMAGE:\$VERSION"
    docker tag "\$IMAGE_ID" "\$IMAGE:latest"
    echo "Tagged \$IMAGE_ID as \$IMAGE:\$VERSION and \$IMAGE:latest"
else
    echo "WARNING: Could not parse image ID from load output — tags may be from save"
fi

echo "--- Ensuring external volumes + network exist ---"
docker volume create etil-sessions 2>/dev/null || true
docker volume create etil-library 2>/dev/null || true
docker network create etil-net 2>/dev/null || true

echo "--- Building env file for compose ---"
rm -f "\$DEPLOY_ENV_FILE"

# Seed from whatever the previous container had; on remote this was
# already gathered via the "Extracting env vars" step above and
# uploaded to \$ENV_FILE. Host-specific paths are NOT backfilled here;
# they must be present in the operator-maintained deploy/production/.env
# on the target server. `docker compose` \${VAR:?} references will fail
# loudly if any required var is missing.
if [ -f "\$ENV_FILE" ]; then
    cp "\$ENV_FILE" "\$DEPLOY_ENV_FILE"
else
    touch "\$DEPLOY_ENV_FILE"
fi

chmod 600 "\$DEPLOY_ENV_FILE"
echo "Env file keys (values masked):"
sed 's/=.*/=***/' "\$DEPLOY_ENV_FILE"

echo "--- compose up -d ---"
if docker compose version >/dev/null 2>&1; then
    docker compose -f "\$COMPOSE_FILE" --env-file "\$DEPLOY_ENV_FILE" up -d
elif command -v docker-compose >/dev/null 2>&1; then
    docker-compose -f "\$COMPOSE_FILE" --env-file "\$DEPLOY_ENV_FILE" up -d
else
    echo "ERROR: neither 'docker compose' plugin nor 'docker-compose' binary on target" >&2
    exit 1
fi

echo "compose up -d completed"
REMOTE_DEPLOY
    fi
    echo ""
fi  # end remote deploy

# --- Step 8: Wait for server ---
echo "=== Waiting for server ==="
if [ "$DRY_RUN" = true ]; then
    echo "[dry-run] Poll localhost:8080/mcp up to 30 times"
else
    _curl_check() {
        curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/mcp 2>/dev/null || true
    }

    SERVER_READY=false
    for i in $(seq 1 30); do
        if [ "$LOCAL_DEPLOY" = true ]; then
            HTTP_CODE=$(_curl_check)
        else
            HTTP_CODE=$($SSH_CMD "curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/mcp 2>/dev/null || true")
        fi
        if [ "$HTTP_CODE" != "000" ]; then
            echo "Server responding (HTTP $HTTP_CODE, attempt $i)"
            SERVER_READY=true
            break
        fi
        echo "  Waiting... (attempt $i/30)"
        sleep 2
    done

    if [ "$SERVER_READY" = false ]; then
        echo "ERROR: Server not responding after 60 seconds"
        echo "  Check logs: docker logs $ETIL_CONTAINER_NAME"
        exit 1
    fi
fi
echo ""

# --- Step 9: Smoke test ---
echo "=== Smoke test ==="
if [ "$DRY_RUN" = true ]; then
    echo "[dry-run] MCP initialize + interpret '42 . cr' + verify output"
else
    _run_smoke_test() {
        set -euo pipefail
        API_KEY=$(docker inspect etil-mcp-http --format '{{range .Config.Env}}{{println .}}{{end}}' | grep ETIL_MCP_API_KEY | cut -d= -f2-)
        URL="http://127.0.0.1:8080/mcp"
        AUTH="Authorization: Bearer $API_KEY"
        CT="Content-Type: application/json"

        INIT_HEADERS=$(mktemp)
        curl -s -D "$INIT_HEADERS" -o /dev/null -X POST "$URL" -H "$CT" -H "$AUTH" \
            -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"deploy","version":"1.0"}}}'
        SID=$(grep '^Mcp-Session-Id:' "$INIT_HEADERS" | tr -d '\r' | awk '{print $2}')
        rm -f "$INIT_HEADERS"

        if [ -z "$SID" ]; then
            echo "FAIL: No session ID from initialize"
            return 1
        fi
        echo "Session: $SID"

        RESULT=$(curl -s -X POST "$URL" -H "$CT" -H "$AUTH" -H "Mcp-Session-Id: $SID" \
            -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"interpret","arguments":{"code":"42 . cr"}}}')

        if echo "$RESULT" | grep -q "42"; then
            echo "PASS: interpret returned 42"
        else
            echo "FAIL: expected 42 in output, got: $RESULT"
            return 1
        fi
    }

    SMOKE_MAX_RETRIES=3
    SMOKE_RETRY_DELAY=10

    if [ "$LOCAL_DEPLOY" = true ]; then
        SMOKE_PASSED=false
        for attempt in $(seq 1 $SMOKE_MAX_RETRIES); do
            if SMOKE_RESULT=$(_run_smoke_test 2>&1); then
                SMOKE_PASSED=true
                break
            fi
            if [ "$attempt" -lt "$SMOKE_MAX_RETRIES" ]; then
                echo "  Smoke test attempt $attempt/$SMOKE_MAX_RETRIES failed, retrying in ${SMOKE_RETRY_DELAY}s..."
                sleep "$SMOKE_RETRY_DELAY"
            fi
        done
    else
        SMOKE_PASSED=false
        for attempt in $(seq 1 $SMOKE_MAX_RETRIES); do
            if SMOKE_RESULT=$($SSH_CMD "bash -s" <<'SMOKE_REMOTE'
set -euo pipefail
API_KEY=$(docker inspect etil-mcp-http --format '{{range .Config.Env}}{{println .}}{{end}}' | grep ETIL_MCP_API_KEY | cut -d= -f2-)
URL="http://127.0.0.1:8080/mcp"
AUTH="Authorization: Bearer $API_KEY"
CT="Content-Type: application/json"

INIT_HEADERS=$(mktemp)
curl -s -D "$INIT_HEADERS" -o /dev/null -X POST "$URL" -H "$CT" -H "$AUTH" \
    -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"deploy","version":"1.0"}}}'
SID=$(grep '^Mcp-Session-Id:' "$INIT_HEADERS" | tr -d '\r' | awk '{print $2}')
rm -f "$INIT_HEADERS"

if [ -z "$SID" ]; then
    echo "FAIL: No session ID from initialize"
    exit 1
fi
echo "Session: $SID"

RESULT=$(curl -s -X POST "$URL" -H "$CT" -H "$AUTH" -H "Mcp-Session-Id: $SID" \
    -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"interpret","arguments":{"code":"42 . cr"}}}')

if echo "$RESULT" | grep -q "42"; then
    echo "PASS: interpret returned 42"
else
    echo "FAIL: expected 42 in output, got: $RESULT"
    exit 1
fi
SMOKE_REMOTE
            ); then
                SMOKE_PASSED=true
                break
            fi
            if [ "$attempt" -lt "$SMOKE_MAX_RETRIES" ]; then
                echo "  Smoke test attempt $attempt/$SMOKE_MAX_RETRIES failed, retrying in ${SMOKE_RETRY_DELAY}s..."
                sleep "$SMOKE_RETRY_DELAY"
            fi
        done
    fi

    echo "$SMOKE_RESULT"
    if [ "$SMOKE_PASSED" = false ]; then
        echo ""
        echo "ERROR: Smoke test failed after $SMOKE_MAX_RETRIES attempts"
        exit 1
    fi
fi
echo ""

# --- Summary ---
ELAPSED=$((SECONDS - START_TIME))
MINS=$((ELAPSED / 60))
SECS=$((ELAPSED % 60))

echo "=== Deploy complete ==="
echo "Version:   v$VERSION"
echo "Container: $ETIL_CONTAINER_NAME"
if [ "$LOCAL_DEPLOY" = true ]; then
    echo "Mode:      local"
else
    echo "Endpoint:  https://$ETIL_SSH_HOST/mcp (via nginx)"
fi
echo "Elapsed:   ${MINS}m ${SECS}s"
