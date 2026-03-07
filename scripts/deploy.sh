#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# Deploy ETIL MCP Docker image to production server from local machine.
#
# Automates: build, save, transfer, load, restart, smoke test.
#
# Usage (from workspace/):
#   evolutionary-til/scripts/deploy.sh              # full build + deploy
#   evolutionary-til/scripts/deploy.sh --skip-build  # reuse existing local image
#   evolutionary-til/scripts/deploy.sh --dry-run     # show plan, don't execute
#
# Prerequisites:
#   - Docker installed locally
#   - SSH key configured via ETIL_SSH_KEY env var (or .ssh/deploy.ed25519)
#   - SSH connectivity to the production server (ETIL_SSH_HOST)

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

for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=true ;;
        --dry-run)    DRY_RUN=true ;;
        --help|-h)
            echo "Usage: $0 [--skip-build] [--dry-run] [--help]"
            echo ""
            echo "  --skip-build   Reuse existing local Docker image (skip build step)"
            echo "  --dry-run      Show what would be done without executing"
            echo "  --help         Show this help"
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
echo "=== ETIL Deploy to production server ==="
echo "Version:    v$VERSION"
echo "Image:      $ETIL_IMAGE_NAME:$VERSION"
echo "Container:  $ETIL_CONTAINER_NAME"
echo "Skip build: $SKIP_BUILD"
echo "Dry run:    $DRY_RUN"
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
    if [ "$DRY_RUN" = false ]; then
        $SSH_CMD "rm -f $REMOTE_TARBALL $REMOTE_ENV_FILE /tmp/etil-deploy-env-final" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# --- Step 1: Verify prerequisites ---
echo "=== Verifying prerequisites ==="

if [ ! -f "$ETIL_SSH_KEY" ]; then
    echo "ERROR: SSH key not found: $ETIL_SSH_KEY"
    exit 1
fi
echo "SSH key:    OK"

if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: docker not found"
    exit 1
fi
echo "Docker:     OK"

if [ "$DRY_RUN" = false ]; then
    if ! $SSH_CMD "echo ok" >/dev/null 2>&1; then
        echo "ERROR: Cannot connect to $ETIL_SSH_HOST"
        exit 1
    fi
    echo "SSH access: OK"
else
    echo "[dry-run] SSH access: skipped"
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
    # Try to extract ETIL_* env vars from the running container.
    # Write them to a temp file on production server to avoid shell quoting issues.
    # Falls back to local .env file if container isn't running (first deploy).
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
        # Upload the .env file contents as ETIL_* vars
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

# Extract image ID from docker load output.
# Output can be "Loaded image: etil-mcp:X.Y.Z" or "Loaded image ID: sha256:abc..."
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
# Build env file (avoids shell expansion issues with \$external in MongoDB URI)
DEPLOY_ENV_FILE="/tmp/etil-deploy-env-final"
rm -f "\$DEPLOY_ENV_FILE"

# Start with extracted env vars (if any)
if [ -f "\$ENV_FILE" ]; then
    cp "\$ENV_FILE" "\$DEPLOY_ENV_FILE"
else
    touch "\$DEPLOY_ENV_FILE"
fi

# Ensure required env vars are set (append only if missing)
grep -q "ETIL_SESSIONS_DIR" "\$DEPLOY_ENV_FILE" || echo "ETIL_SESSIONS_DIR=/data/sessions" >> "\$DEPLOY_ENV_FILE"
grep -q "ETIL_LIBRARY_DIR" "\$DEPLOY_ENV_FILE" || echo "ETIL_LIBRARY_DIR=/data/library" >> "\$DEPLOY_ENV_FILE"
# ETIL_AUTH_CONFIG is now a directory path (was a file path before v0.8.4)
if grep -q "ETIL_AUTH_CONFIG" "\$DEPLOY_ENV_FILE"; then
    sed -i 's|^ETIL_AUTH_CONFIG=.*|ETIL_AUTH_CONFIG=/etc/etil|' "\$DEPLOY_ENV_FILE"
else
    echo "ETIL_AUTH_CONFIG=/etc/etil" >> "\$DEPLOY_ENV_FILE"
fi

# Ensure MongoDB env vars are set (x.509 TLS via host.docker.internal)
if ! grep -q "ETIL_MONGODB_URI" "\$DEPLOY_ENV_FILE"; then
    # Read MongoDB URI from /opt/mongodb/etil-uri if available (avoids shell escaping of \$external)
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
    -v /opt/etil/oauth:/etc/etil:ro \
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

# --- Step 8: Wait for server ---
echo "=== Waiting for server ==="
if [ "$DRY_RUN" = true ]; then
    echo "[dry-run] Poll localhost:8080/mcp up to 30 times"
else
    SERVER_READY=false
    for i in $(seq 1 30); do
        HTTP_CODE=$($SSH_CMD "curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/mcp 2>/dev/null" || echo "000")
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
        echo "  Check logs: ssh production server 'docker logs $ETIL_CONTAINER_NAME'"
        exit 1
    fi
fi
echo ""

# --- Step 9: Smoke test ---
echo "=== Smoke test ==="
if [ "$DRY_RUN" = true ]; then
    echo "[dry-run] MCP initialize + interpret '42 . cr' + verify output"
else
    SMOKE_RESULT=$($SSH_CMD "bash -s" <<'SMOKE_TEST'
set -euo pipefail
API_KEY=$(docker inspect etil-mcp-http --format '{{range .Config.Env}}{{println .}}{{end}}' | grep ETIL_MCP_API_KEY | cut -d= -f2)
URL="http://127.0.0.1:8080/mcp"
AUTH="Authorization: Bearer $API_KEY"
CT="Content-Type: application/json"

# Initialize — capture session ID
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

# Interpret 42 . cr
RESULT=$(curl -s -X POST "$URL" -H "$CT" -H "$AUTH" -H "Mcp-Session-Id: $SID" \
    -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"interpret","arguments":{"code":"42 . cr"}}}')

if echo "$RESULT" | grep -q "42"; then
    echo "PASS: interpret returned 42"
else
    echo "FAIL: expected 42 in output, got: $RESULT"
    exit 1
fi
SMOKE_TEST
    )

    echo "$SMOKE_RESULT"
    if echo "$SMOKE_RESULT" | grep -q "FAIL"; then
        echo ""
        echo "ERROR: Smoke test failed"
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
echo "Endpoint:  https://$ETIL_SSH_HOST/mcp (via nginx)"
echo "Elapsed:   ${MINS}m ${SECS}s"
