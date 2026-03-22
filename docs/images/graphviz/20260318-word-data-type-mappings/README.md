# ETIL GraphViz Diagrams

Architecture and word/type-map diagrams for ETIL, generated with GraphViz and Python.

## Prerequisites

```bash
sudo apt install graphviz python3-numpy
```

## Quick Start

```bash
cd docs/images/graphviz
bash scripts/render_all.sh
# PNGs appear in output/
```

## Scripts

| Script | Description |
|--------|-------------|
| `gen_architecture.dot` | Interpreter class diagram (static, renders with `dot`) |
| `gen_source_tree.dot` | Source file tree with line counts (static, renders with `dot`) |
| `gen_type_map_radial.py` | Radial layout — categories as clusters around type hexagons |
| `gen_type_map_ring.py` | Ring layout — all words on a single ring, category order |
| `gen_type_map_ring_spectral.py` | Spectral ordering + alternating optimization on a ring |
| `gen_type_map_grid.py` | Grid layout with category-cohesion force — best result |
| `render_all.sh` | Renders everything into `output/`, including a cohesion sweep |

## Grid Cohesion Weight

The grid script accepts a cohesion weight as its first argument (default: 1.2).
Higher values keep category clusters tighter; lower values let words migrate
toward their connected type hexagons.

```bash
# Single run with custom weight
python3 scripts/gen_type_map_grid.py 1.15 output/
neato -n -Tpng -o output/etil-type-grid-1.15.png output/etil-type-grid-1.15.dot
```

The `render_all.sh` script sweeps 1.05 to 1.35 in 0.05 steps so you can
compare results visually.

## Python Script CLI

All Python scripts accept an output directory as their last positional argument
(default: current directory). They write `.dot` files which are then rendered
with `neato -n`.

```bash
python3 scripts/gen_type_map_radial.py /tmp/   # writes /tmp/etil-type-map-radial.dot
python3 scripts/gen_type_map_ring.py /tmp/      # writes /tmp/etil-type-ring.dot
python3 scripts/gen_type_map_grid.py 1.2 /tmp/  # writes /tmp/etil-type-grid-1.20.dot
```
