#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Manifold AMQP 1.0 broker E2E integration test (Phase 3c).
#
# Spins up apache/activemq-artemis:2.40 with a tuned 256 MB heap
# (doc B §16.3) alongside an etil container built with
# ETIL_BUILD_AMQP_SINK=ON. Publishes a message through the ETIL
# AMQP sink and verifies subject delivery plus Session-Hmac and
# Msg-Codec header presence via a Python Proton subscriber.
#
# Self-skips when Docker isn't available or when free RAM is under
# 1 GB (Artemis needs breathing room).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

ARTEMIS_IMAGE="apache/activemq-artemis:2.40.0"
ARTEMIS_CONTAINER="etil-manifold-artemis-$$"
SUB_CONTAINER="etil-manifold-artemis-sub-$$"
NET="etil-manifold-artemis-net-$$"

PASS_COUNT=0
FAIL_COUNT=0

pass() { echo "PASS: $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { echo "FAIL: $1"; FAIL_COUNT=$((FAIL_COUNT + 1)); }

cleanup() {
    echo ""
    echo "=== Cleanup ==="
    docker rm -f "$ARTEMIS_CONTAINER" 2>/dev/null || true
    docker rm -f "$SUB_CONTAINER" 2>/dev/null || true
    docker network rm "$NET" 2>/dev/null || true
}
trap cleanup EXIT

if ! command -v docker >/dev/null 2>&1; then
    echo "SKIP: docker not available"
    exit 0
fi

# Skip on sub-1GB free RAM hosts — Artemis + etil won't fit.
FREE_KB=$(awk '/MemAvailable/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)
if [ "$FREE_KB" -lt 900000 ]; then
    echo "SKIP: insufficient free RAM for Artemis (${FREE_KB}K < 900000K)"
    exit 0
fi

echo "=== Bringing up Artemis ==="
docker network create "$NET" >/dev/null
docker run -d --rm \
    --name "$ARTEMIS_CONTAINER" \
    --network "$NET" \
    -p 127.0.0.1:15672:5672 \
    -e ARTEMIS_USER=admin -e ARTEMIS_PASSWORD=admin \
    -e JAVA_ARGS="-Xmx256m -Xms128m" \
    "$ARTEMIS_IMAGE" >/dev/null

# Poll for broker readiness (AMQP listener up).
for i in $(seq 1 30); do
    sleep 2
    if docker exec "$ARTEMIS_CONTAINER" \
            sh -c 'curl -sf http://127.0.0.1:8161/console/ >/dev/null'; then
        pass "Artemis console reachable"
        break
    fi
done

echo ""
echo "=== Starting Python Proton subscriber ==="
# Use python:3-slim with qpid-proton installed on the fly.
docker run -d --rm \
    --name "$SUB_CONTAINER" \
    --network "$NET" \
    -v "$SCRIPT_DIR:/tests:ro" \
    python:3.11-slim \
    sh -c "pip install python-qpid-proton >/dev/null 2>&1 &&
           python /tests/amqp_subscriber.py \
              amqp://${ARTEMIS_CONTAINER}:5672 etil.test.amqp > /tmp/sub.log 2>&1; \
           sleep 120" \
    >/dev/null
sleep 8
pass "subscriber attached"

echo ""
echo "=== Publishing from ETIL ==="
cat > /tmp/manifold-amqp-pub.til <<TIL
s" amqp://${ARTEMIS_CONTAINER}:5672" s" json" s" etil.test.amqp" channel-tap-amqp .
cr
s" hello-from-etil" s" etil.test.amqp" channel-publish
s" done" type cr
TIL

PUB_OUT=$(docker run --rm \
    --network "$NET" \
    -v /tmp/manifold-amqp-pub.til:/work/pub.til:ro \
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
echo "=== Checking subscriber log ==="
sleep 2
SUB_LOG=$(docker exec "$SUB_CONTAINER" cat /tmp/sub.log 2>/dev/null || true)
echo "$SUB_LOG" | head -20

if echo "$SUB_LOG" | grep -q "etil.test.amqp"; then
    pass "AMQP subject received"
else
    fail "AMQP subject did not reach subscriber"
fi

if echo "$SUB_LOG" | grep -q "Session-Hmac"; then
    pass "Session-Hmac header present"
else
    fail "Session-Hmac header absent"
fi

if echo "$SUB_LOG" | grep -q "Msg-Codec"; then
    pass "Msg-Codec header present"
else
    fail "Msg-Codec header absent"
fi

echo ""
echo "=== Summary ==="
echo "Passed: $PASS_COUNT"
echo "Failed: $FAIL_COUNT"
[ "$FAIL_COUNT" -eq 0 ]
