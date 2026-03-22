#!/usr/bin/env bash
# Render all ETIL GraphViz diagrams into ../output/
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/../output"
mkdir -p "$OUTPUT_DIR"

echo "=== Rendering ETIL architecture diagrams ==="
echo "Output: $OUTPUT_DIR"
echo

# ── Static .dot files (parallel) ──
echo "[1/4] Architecture class diagram (dot)..."
dot -Tpng -o "$OUTPUT_DIR/etil-architecture.png" "$SCRIPT_DIR/gen_architecture.dot" &
PID_ARCH=$!

echo "[2/4] Source tree diagram (dot)..."
dot -Tpng -o "$OUTPUT_DIR/etil-source-tree.png" "$SCRIPT_DIR/gen_source_tree.dot" &
PID_TREE=$!

# ── Radial type map ──
echo "[3/4] Radial type map (python + neato)..."
python3 "$SCRIPT_DIR/gen_type_map_radial.py" "$OUTPUT_DIR"
neato -n -Tpng -o "$OUTPUT_DIR/etil-type-map-radial.png" "$OUTPUT_DIR/etil-type-map-radial.dot" &
PID_RADIAL=$!

# ── Ring type map ──
echo "[4/4] Ring type map (python + neato)..."
python3 "$SCRIPT_DIR/gen_type_map_ring.py" "$OUTPUT_DIR"
neato -n -Tpng -o "$OUTPUT_DIR/etil-type-ring.png" "$OUTPUT_DIR/etil-type-ring.dot" &
PID_RING=$!

# Wait for fast renders
wait $PID_ARCH $PID_TREE $PID_RADIAL $PID_RING
echo

# ── Spectral ring (slower — sequential) ──
echo "[5] Spectral-optimized ring (python + neato)..."
python3 "$SCRIPT_DIR/gen_type_map_ring_spectral.py" "$OUTPUT_DIR"
neato -n -Tpng -o "$OUTPUT_DIR/etil-type-ring-spectral.png" "$OUTPUT_DIR/etil-type-ring-spectral.dot"
echo

# ── Grid layout — cohesion sweep ──
echo "[6] Grid layout — cohesion sweep (1.05 to 1.35)..."
for w in 1.05 1.10 1.15 1.20 1.25 1.30 1.35; do
    echo "  cohesion=$w..."
    python3 "$SCRIPT_DIR/gen_type_map_grid.py" "$w" "$OUTPUT_DIR"
    neato -n -Tpng \
        -o "$OUTPUT_DIR/etil-type-grid-${w}.png" \
        "$OUTPUT_DIR/etil-type-grid-${w}.dot"
done
echo

# ── Clean up intermediate .dot files from Python generators ──
rm -f "$OUTPUT_DIR"/*.dot

echo "=== Done ==="
ls -lh "$OUTPUT_DIR"/*.png
