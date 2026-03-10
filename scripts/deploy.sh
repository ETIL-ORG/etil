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
#   - SSH key configured via ETIL_SSH_KEY env var (or .ssh/deploy.ed25519)
#   - SSH connectivity to the production server (ETIL_SSH_HOST)
# Prerequisites (local-deploy mode):
#   - Docker installed locally
#   - Release binary at $ETIL_BUILD_RELEASE_DIR/bin/etil_mcp_server

set -euo pipefail

# --- Common environment ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"

# --- Derived constants ---
SSH_CMD="ssh -i $ETIL_SSH_KEY $ETIL_SSH_HOST"
SCP_CMD="scp -i $ETIL_SSH_KEY"
LOCAL_TARBALL="/tmp/etil-mcp-deploy.tar.gz"
REMOTE_TARBALL="/tmp/etil-mcp-deploy.tar.gz"
REMOTE_ENV_FILE="/tmp/etil-deploy-env"
ENV_FALLBACK="$ETIL_PROJECT_DIR/deploy/production/.env"
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
        $SSH_CMD "rm -f $REMOTE_TARBALL $REMOTE_ENV_FILE /tmp/etil-deploy-env-final" 2>/dev/null || true
    fi
    if [ "$LOCAL_DEPLOY" = true ]; then
        rm -f /tmp/etil-deploy-env /tmp/etil-deploy-env-final 2>/dev/null || true
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
    if [ ! -f "$ETIL_SSH_KEY" ]; then
        echo "ERROR: SSH key not found: $ETIL_SSH_KEY"
        exit 1
    fi
    echo "SSH key:    OK"

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
    echo "=== Local deploy: restarting container ==="
    if [ "$DRY_RUN" = true ]; then
        echo "[dry-run] docker stop + rm + run locally"
    else
        # Extract env vars from running container
        ENV_FILE="/tmp/etil-deploy-env"
        DEPLOY_ENV_FILE="/tmp/etil-deploy-env-final"
        rm -f "$ENV_FILE" "$DEPLOY_ENV_FILE"

        if docker inspect "$ETIL_CONTAINER_NAME" >/dev/null 2>&1; then
            docker inspect "$ETIL_CONTAINER_NAME" --format '{{range .Config.Env}}{{println .}}{{end}}' \
                | grep '^ETIL_' > "$ENV_FILE" 2>/dev/null || true
            echo "Env vars extracted from running container"
        elif [ -f "$ENV_FALLBACK" ]; then
            grep '^ETIL_' "$ENV_FALLBACK" > "$ENV_FILE" 2>/dev/null || true
            echo "Env vars loaded from fallback .env"
        else
            touch "$ENV_FILE"
            echo "WARNING: No env vars found"
        fi

        cp "$ENV_FILE" "$DEPLOY_ENV_FILE"

        # Ensure required env vars
        grep -q "ETIL_SESSIONS_DIR" "$DEPLOY_ENV_FILE" || echo "ETIL_SESSIONS_DIR=/data/sessions" >> "$DEPLOY_ENV_FILE"
        grep -q "ETIL_LIBRARY_DIR" "$DEPLOY_ENV_FILE" || echo "ETIL_LIBRARY_DIR=/data/library" >> "$DEPLOY_ENV_FILE"
        if grep -q "ETIL_AUTH_CONFIG" "$DEPLOY_ENV_FILE"; then
            sed -i 's|^ETIL_AUTH_CONFIG=.*|ETIL_AUTH_CONFIG=/etc/etil|' "$DEPLOY_ENV_FILE"
        else
            echo "ETIL_AUTH_CONFIG=/etc/etil" >> "$DEPLOY_ENV_FILE"
        fi
        if ! grep -q "ETIL_MONGODB_URI" "$DEPLOY_ENV_FILE"; then
            if [ -f /opt/mongodb/etil-uri ]; then
                echo "ETIL_MONGODB_URI=$(cat /opt/mongodb/etil-uri)" >> "$DEPLOY_ENV_FILE"
            else
                echo "ETIL_MONGODB_URI=mongodb://host.docker.internal:27017" >> "$DEPLOY_ENV_FILE"
            fi
        fi
        grep -q "ETIL_MONGODB_DATABASE" "$DEPLOY_ENV_FILE" || echo "ETIL_MONGODB_DATABASE=etil" >> "$DEPLOY_ENV_FILE"

        echo "Env file contents (values masked):"
        sed 's/=.*/=***/' "$DEPLOY_ENV_FILE"

        docker stop "$ETIL_CONTAINER_NAME" 2>/dev/null || true
        docker rm "$ETIL_CONTAINER_NAME" 2>/dev/null || true
        docker volume create etil-sessions 2>/dev/null || true
        docker volume create etil-library 2>/dev/null || true

        docker run -d --name "$ETIL_CONTAINER_NAME" --restart unless-stopped \
            --read-only --security-opt no-new-privileges:true \
            --tmpfs /tmp:size=10M \
            -p 127.0.0.1:8080:8080 \
            --add-host host.docker.internal:host-gateway \
            -v etil-sessions:/data/sessions \
            -v etil-library:/data/library:ro \
            -v /opt/etil/oauth:/etc/etil \
            -v /opt/mongodb/client-etil.pem:/etc/etil-mongo/client-etil.pem:ro \
            -v /opt/mongodb/ca.pem:/etc/etil-mongo/ca.pem:ro \
            --group-add "${ETIL_CERT_GID:-1006}" \
            --group-add "${ETIL_MONGO_GID:-110}" \
            --memory 512m --memory-swap 512m \
            --cpus 1.0 --pids-limit 50 \
            --log-driver json-file --log-opt max-size=10m --log-opt max-file=3 \
            --env-file "$DEPLOY_ENV_FILE" \
            "$ETIL_IMAGE_NAME:latest" --host 0.0.0.0 --port 8080

        rm -f "$ENV_FILE" "$DEPLOY_ENV_FILE"
        echo "Container started"
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
    else
        $SCP_CMD "$LOCAL_TARBALL" "$ETIL_SSH_HOST:$REMOTE_TARBALL"
        echo "Transfer complete"
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

echo "--- Stopping old container ---"
docker stop "\$CONTAINER" 2>/dev/null || true
docker rm "\$CONTAINER" 2>/dev/null || true

echo "--- Creating volumes ---"
docker volume create etil-sessions 2>/dev/null || true
docker volume create etil-library 2>/dev/null || true

echo "--- Building env file for container ---"
DEPLOY_ENV_FILE="/tmp/etil-deploy-env-final"
rm -f "\$DEPLOY_ENV_FILE"

if [ -f "\$ENV_FILE" ]; then
    cp "\$ENV_FILE" "\$DEPLOY_ENV_FILE"
else
    touch "\$DEPLOY_ENV_FILE"
fi

grep -q "ETIL_SESSIONS_DIR" "\$DEPLOY_ENV_FILE" || echo "ETIL_SESSIONS_DIR=/data/sessions" >> "\$DEPLOY_ENV_FILE"
grep -q "ETIL_LIBRARY_DIR" "\$DEPLOY_ENV_FILE" || echo "ETIL_LIBRARY_DIR=/data/library" >> "\$DEPLOY_ENV_FILE"
if grep -q "ETIL_AUTH_CONFIG" "\$DEPLOY_ENV_FILE"; then
    sed -i 's|^ETIL_AUTH_CONFIG=.*|ETIL_AUTH_CONFIG=/etc/etil|' "\$DEPLOY_ENV_FILE"
else
    echo "ETIL_AUTH_CONFIG=/etc/etil" >> "\$DEPLOY_ENV_FILE"
fi

if ! grep -q "ETIL_MONGODB_URI" "\$DEPLOY_ENV_FILE"; then
    if [ -f /opt/mongodb/etil-uri ]; then
        echo "ETIL_MONGODB_URI=\$(cat /opt/mongodb/etil-uri)" >> "\$DEPLOY_ENV_FILE"
    else
        echo "ETIL_MONGODB_URI=mongodb://host.docker.internal:27017" >> "\$DEPLOY_ENV_FILE"
    fi
fi
grep -q "ETIL_MONGODB_DATABASE" "\$DEPLOY_ENV_FILE" || echo "ETIL_MONGODB_DATABASE=etil" >> "\$DEPLOY_ENV_FILE"

echo "Env file contents (values masked):"
sed 's/=.*/=***/' "\$DEPLOY_ENV_FILE"

echo "--- Starting container ---"
docker run -d --name "\$CONTAINER" --restart unless-stopped \
    --read-only --security-opt no-new-privileges:true \
    --tmpfs /tmp:size=10M \
    -p 127.0.0.1:8080:8080 \
    --add-host host.docker.internal:host-gateway \
    -v etil-sessions:/data/sessions \
    -v etil-library:/data/library:ro \
    -v /opt/etil/oauth:/etc/etil \
    -v /opt/mongodb/client-etil.pem:/etc/etil-mongo/client-etil.pem:ro \
    -v /opt/mongodb/ca.pem:/etc/etil-mongo/ca.pem:ro \
    --group-add "${ETIL_CERT_GID:-1006}" \
    --group-add "${ETIL_MONGO_GID:-110}" \
    --memory 512m --memory-swap 512m \
    --cpus 1.0 --pids-limit 50 \
    --log-driver json-file --log-opt max-size=10m --log-opt max-file=3 \
    --env-file "\$DEPLOY_ENV_FILE" \
    "\$IMAGE:latest" --host 0.0.0.0 --port 8080

rm -f "\$DEPLOY_ENV_FILE"

echo "Container started"
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
        API_KEY=$(docker inspect etil-mcp-http --format '{{range .Config.Env}}{{println .}}{{end}}' | grep ETIL_MCP_API_KEY | cut -d= -f2)
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
API_KEY=$(docker inspect etil-mcp-http --format '{{range .Config.Env}}{{println .}}{{end}}' | grep ETIL_MCP_API_KEY | cut -d= -f2)
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
