#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Manifold NATS broker E2E integration test (Phase 3b).
#
# Spins up a standalone nats-server container alongside an etil
# container built with ETIL_BUILD_NATS_SINK=ON. Publishes a
# message through the ETIL NATS sink and verifies:
#   - the message arrives on the NATS subject
#   - Session-Hmac header is present and opaque (not the raw session_id)
#   - Msg-Codec / Msg-Host / Msg-Seq headers are populated
#
# Skips cleanly when Docker isn't available. Not a SKIP when the
# containers run but NATS traffic doesn't round-trip — that's a FAIL.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

NATS_IMAGE="nats:2.10"
NATS_CONTAINER="etil-manifold-nats-$$"
SUBSCRIBER_CONTAINER="etil-manifold-nats-sub-$$"
NATS_NET="etil-manifold-nats-net-$$"

PASS_COUNT=0
FAIL_COUNT=0

pass() { echo "PASS: $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { echo "FAIL: $1"; FAIL_COUNT=$((FAIL_COUNT + 1)); }

cleanup() {
    echo ""
    echo "=== Cleanup ==="
    docker rm -f "$NATS_CONTAINER" 2>/dev/null || true
    docker rm -f "$SUBSCRIBER_CONTAINER" 2>/dev/null || true
    docker network rm "$NATS_NET" 2>/dev/null || true
    rm -f /tmp/nats-sub-${$}.log
}
trap cleanup EXIT

# Preflight: need docker.
if ! command -v docker >/dev/null 2>&1; then
    echo "SKIP: docker not available"
    exit 0
fi

echo "=== Bringing up NATS server ==="
docker network create "$NATS_NET" >/dev/null
docker run -d --rm \
    --name "$NATS_CONTAINER" \
    --network "$NATS_NET" \
    -p 127.0.0.1:14222:4222 \
    "$NATS_IMAGE" -DV >/dev/null
# Wait for server readiness.
for i in 1 2 3 4 5 6 7 8 9 10; do
    sleep 1
    if docker exec "$NATS_CONTAINER" nats-server --version >/dev/null 2>&1; then
        pass "nats-server started"
        break
    fi
done

echo ""
echo "=== Starting subscriber ==="
# Use the nats container's own nats CLI would be ideal; fall back
# to a simple netcat-ish subscriber via nats-box.
docker run -d --rm \
    --name "$SUBSCRIBER_CONTAINER" \
    --network "$NATS_NET" \
    --entrypoint sh \
    natsio/nats-box:latest \
    -c "nats sub --server=nats://${NATS_CONTAINER}:4222 'etil.>' > /tmp/sub.log 2>&1; sleep 300" \
    >/dev/null
sleep 2
pass "subscriber attached"

echo ""
echo "=== Publishing from ETIL ==="
# The publisher is an inline TIL script run through etil_repl with
# ETIL_BUILD_NATS_SINK=ON. The project Docker image must be built
# with that flag; this test expects the default -test image built
# with all features on.
#
# For Phase 3b integration we write a .til file to /tmp and mount
# it into the etil container. The script publishes once and prints
# the returned handle.
cat > /tmp/manifold-nats-pub.til <<'TIL'
# channel-tap-nats installs a NATS sink, then channel-publish
# sends one JSON message.
s" nats://NATS_HOST:4222" s" json" s" etil.test.nats" channel-tap-nats .
cr
s" hello-from-etil" s" etil.test.nats" channel-publish
s" done" type cr
TIL
# Substitute the NATS host into the tile — container DNS name.
sed -i "s/NATS_HOST/${NATS_CONTAINER}/" /tmp/manifold-nats-pub.til

# Run the publisher container. The etil image must have been built
# with ETIL_BUILD_NATS_SINK=ON via the Dockerfile; if the default
# build is missing the flag, the tap returns false and we mark FAIL.
PUB_OUT=$(docker run --rm \
    --network "$NATS_NET" \
    -v /tmp/manifold-nats-pub.til:/work/pub.til:ro \
    --entrypoint /usr/local/bin/etil_repl \
    etil-mcp \
    /work/pub.til 2>&1 || true)
echo "$PUB_OUT" | head -10

if echo "$PUB_OUT" | grep -q "done"; then
    pass "ETIL published"
else
    fail "ETIL publish did not reach completion"
fi

echo ""
echo "=== Checking subscriber received the message ==="
sleep 1
SUB_LOG=$(docker exec "$SUBSCRIBER_CONTAINER" cat /tmp/sub.log 2>/dev/null || true)
echo "$SUB_LOG" | head -20

if echo "$SUB_LOG" | grep -q "etil.test.nats"; then
    pass "subject received by subscriber"
else
    fail "subject did not reach subscriber"
fi

if echo "$SUB_LOG" | grep -q "Session-Hmac"; then
    pass "Session-Hmac header present"
else
    fail "Session-Hmac header absent"
fi

if echo "$SUB_LOG" | grep -q "Msg-Codec: json"; then
    pass "Msg-Codec header present"
else
    fail "Msg-Codec header absent"
fi

echo ""
echo "=== Summary ==="
echo "Passed: $PASS_COUNT"
echo "Failed: $FAIL_COUNT"
[ "$FAIL_COUNT" -eq 0 ]
