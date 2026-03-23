#!/usr/bin/env python3
"""
Generate POV-Ray frames for an ETIL EvolutionEngine animation.

Renders a branching evolutionary tree where:
- Generations appear progressively
- Side branches grow, pulse, then die off
- The central fittest lineage survives and glows
- Color transitions from cyan → green → amber → gold → ember

Usage:
    python3 animate.py [--fps 15] [--width 1920] [--height 1080] [--quality 9]
    # Then: ffmpeg assembly is printed at the end
"""

import argparse
import math
import os
import subprocess
import sys
import shutil
from dataclasses import dataclass, field

# ============================================================
# Configuration
# ============================================================

FPS = 15
WIDTH = 1920
HEIGHT = 1080
QUALITY = 9
AA = 0.3

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FRAMES_DIR = os.path.join(SCRIPT_DIR, "frames")
OUTPUT_FILE = os.path.join(SCRIPT_DIR, "evolution-animation.mp4")

# ============================================================
# Timeline (seconds)
# ============================================================

FADE_IN = 0.5
FADE_OUT = 0.5
HOLD_END = 2.5        # hold final frame for looping
GEN_DURATION = 0.8    # time per generation to grow in
TOTAL_GENS = 10       # G0-G9

# Total animation time
ANIM_DURATION = FADE_IN + (TOTAL_GENS * GEN_DURATION) + HOLD_END + FADE_OUT

# ============================================================
# Color palette (rgb tuples, 0-1)
# ============================================================

COL_CYAN    = (0.31, 0.76, 0.97)
COL_TEAL    = (0.15, 0.78, 0.85)
COL_GREEN   = (0.40, 0.73, 0.42)
COL_LIME    = (0.51, 0.78, 0.52)
COL_AMBER   = (1.00, 0.72, 0.30)
COL_ORANGE  = (1.00, 0.56, 0.00)
COL_DORANGE = (1.00, 0.44, 0.00)
COL_RORANGE = (0.90, 0.32, 0.00)
COL_EMBER   = (0.75, 0.21, 0.05)
COL_DARK    = (0.53, 0.12, 0.02)
COL_ASH     = (0.30, 0.06, 0.00)
COL_DEAD    = (0.12, 0.14, 0.18)

# ============================================================
# Data model
# ============================================================

@dataclass
class Node:
    """An evolutionary variant."""
    name: str
    pos: tuple             # (x, y, z)
    max_radius: float
    color: tuple           # (r, g, b)
    parent: str            # parent node name, "" for root
    appear_gen: float      # generation when this node appears
    die_gen: float         # generation when it dies (-1 = survives)
    is_fittest: bool = False

def lerp(a, b, t):
    t = max(0.0, min(1.0, t))
    return a + (b - a) * t

def lerp_color(c1, c2, t):
    return (lerp(c1[0], c2[0], t), lerp(c1[1], c2[1], t), lerp(c1[2], c2[2], t))

def col_str(c):
    return f"rgb <{c[0]:.3f}, {c[1]:.3f}, {c[2]:.3f}>"

def pov(template, **kwargs):
    """Format a POV-Ray template string, substituting $key with values.
    Replaces longest keys first to avoid partial matches."""
    result = template
    for k in sorted(kwargs.keys(), key=len, reverse=True):
        v = kwargs[k]
        if isinstance(v, float):
            result = result.replace(f"${k}", f"{v:.4f}")
        else:
            result = result.replace(f"${k}", str(v))
    return result

# ============================================================
# Define the evolutionary tree
# ============================================================

def build_tree():
    nodes = []
    n = nodes.append

    # GENERATION 0
    n(Node("G0", (0, 0.5, 0), 0.65, COL_CYAN, "", 0, -1, True))

    # GENERATION 1 — 5 branches, 2 survive long
    n(Node("G1a", (2.5, 0.5, 1.2),  0.48, COL_TEAL, "G0", 1, -1, True))    # main line
    n(Node("G1b", (2.5, 0.5, 2.8),  0.45, COL_TEAL, "G0", 1, 5))            # dies at G5
    n(Node("G1c", (2.3, 0.5, -0.8), 0.42, COL_TEAL, "G0", 1, 3))            # dies at G3
    n(Node("G1d", (2.0, 0.5, -2.0), 0.32, COL_TEAL, "G0", 1, 1.5))          # dies quickly
    n(Node("G1e", (2.2, 0.5, 3.8),  0.32, COL_TEAL, "G0", 1, 1.5))          # dies quickly

    # GENERATION 2
    n(Node("G2a1", (5.0, 0.5, 1.5),  0.42, COL_GREEN, "G1a", 2, -1, True))  # main line
    n(Node("G2a2", (5.0, 0.5, 3.2),  0.40, COL_GREEN, "G1a", 2, 6))         # dies at G6
    n(Node("G2a3", (4.8, 0.5, 0.0),  0.38, COL_GREEN, "G1a", 2, 4))         # dies at G4
    n(Node("G2a4", (4.6, 0.5, -1.2), 0.26, COL_GREEN, "G1a", 2, 2.5))       # dies quickly

    n(Node("G2b1", (5.2, 0.5, 4.2),  0.38, (0.30, 0.71, 0.67), "G1b", 2, 5))
    n(Node("G2b2", (4.8, 0.5, 5.5),  0.26, (0.30, 0.71, 0.67), "G1b", 2, 2.5))
    n(Node("G2b3", (5.0, 0.5, 5.0),  0.26, (0.30, 0.71, 0.67), "G1b", 2, 2.5))

    n(Node("G2c1", (4.5, 0.5, -1.5), 0.32, (0.25, 0.68, 0.72), "G1c", 2, 3))
    n(Node("G2c2", (4.8, 0.5, -2.2), 0.30, (0.28, 0.65, 0.68), "G1c", 2, 3))

    # GENERATION 3
    n(Node("G3a1", (7.5, 0.5, 2.0),  0.40, COL_AMBER, "G2a1", 3, -1, True)) # main line
    n(Node("G3a2", (7.3, 0.5, 3.5),  0.35, COL_AMBER, "G2a1", 3, 7))
    n(Node("G3a3", (7.0, 0.5, 0.5),  0.24, COL_AMBER, "G2a1", 3, 3.5))

    n(Node("G3b1", (7.5, 0.5, 4.5),  0.32, COL_AMBER, "G2a2", 3, 6))
    n(Node("G3b2", (7.2, 0.5, 5.5),  0.22, COL_AMBER, "G2a2", 3, 3.5))

    n(Node("G3c1", (7.2, 0.5, -1.0), 0.28, COL_AMBER, "G2a3", 3, 5))
    n(Node("G3c2", (7.0, 0.5, -2.0), 0.20, COL_AMBER, "G2a3", 3, 3.5))

    n(Node("G3d1", (7.8, 0.5, 5.5),  0.28, (0.80, 0.60, 0.25), "G2b1", 3, 5))
    n(Node("G3d2", (7.5, 0.5, 6.5),  0.20, (0.80, 0.60, 0.25), "G2b1", 3, 3.5))

    n(Node("G3e1", (7.0, 0.5, -2.5), 0.22, COL_DEAD, "G2c1", 3, 3.5))
    n(Node("G3e2", (7.2, 0.5, -3.2), 0.20, COL_DEAD, "G2c2", 3, 3.5))

    # GENERATION 4
    n(Node("G4a1", (10.0, 0.5, 2.5),  0.38, COL_ORANGE, "G3a1", 4, -1, True))
    n(Node("G4a2", (10.0, 0.5, 4.0),  0.30, COL_ORANGE, "G3a1", 4, 7))
    n(Node("G4a3", (9.5, 0.5, 0.8),   0.22, COL_ORANGE, "G3a1", 4, 4.5))

    n(Node("G4b1", (10.2, 0.5, 5.2),  0.28, COL_ORANGE, "G3b1", 4, 6))
    n(Node("G4c1", (10.0, 0.5, 5.8),  0.25, COL_ORANGE, "G3b1", 4, 5))

    n(Node("G4d1", (9.8, 0.5, -0.5),  0.22, (0.90, 0.60, 0.20), "G3c1", 4, 5))
    n(Node("G4e1", (10.2, 0.5, 6.8),  0.22, (0.85, 0.55, 0.18), "G3d1", 4, 5))

    # GENERATION 5
    n(Node("G5a", (12.5, 0.5, 3.0),  0.32, COL_DORANGE, "G4a1", 5, -1, True))
    n(Node("G5b", (12.5, 0.5, 4.5),  0.25, COL_DORANGE, "G4a1", 5, 7))
    n(Node("G5c", (12.8, 0.5, 5.5),  0.22, COL_DORANGE, "G4a2", 5, 6))
    n(Node("G5d", (12.5, 0.5, 6.5),  0.20, (0.95, 0.42, 0.05), "G4b1", 5, 6))

    # GENERATION 6
    n(Node("G6a", (14.8, 0.5, 3.5),  0.28, COL_RORANGE, "G5a", 6, -1, True))
    n(Node("G6b", (14.5, 0.5, 5.0),  0.20, COL_RORANGE, "G5a", 6, 7))
    n(Node("G6c", (14.8, 0.5, 6.0),  0.16, COL_RORANGE, "G5b", 6, 6.5))
    n(Node("G6d", (15.0, 0.5, 6.5),  0.18, COL_RORANGE, "G5c", 6, 7))

    # GENERATION 7
    n(Node("G7a", (16.8, 0.5, 4.0),  0.22, COL_EMBER, "G6a", 7, -1, True))
    n(Node("G7b", (16.5, 0.5, 5.5),  0.15, COL_EMBER, "G6a", 7, 8))

    # GENERATION 8
    n(Node("G8a", (18.5, 0.5, 4.5),  0.18, COL_DARK, "G7a", 8, -1, True))
    n(Node("G8b", (18.2, 0.5, 5.8),  0.12, COL_DARK, "G7a", 8, 9))

    # GENERATION 9
    n(Node("G9a", (20.0, 0.5, 5.0),  0.12, COL_ASH, "G8a", 9, -1, True))
    n(Node("G9b", (21.0, 0.5, 5.5),  0.07, COL_ASH, "G9a", 9, -1))

    return nodes

# ============================================================
# Node state at a given time (generation units)
# ============================================================

def node_state(node, gen_time):
    """Returns (visible, radius, color, opacity) for a node at gen_time."""

    # Not yet appeared
    if gen_time < node.appear_gen:
        return False, 0, node.color, 0

    # Growth phase (0.0 → 1.0 over 0.6 gen units)
    grow_t = (gen_time - node.appear_gen) / 0.6
    grow_t = max(0.0, min(1.0, grow_t))

    alive = True
    death_fade = 1.0

    if node.die_gen > 0 and gen_time >= node.die_gen:
        # Dying phase (fade over 0.8 gen units)
        death_t = (gen_time - node.die_gen) / 0.8
        death_t = max(0.0, min(1.0, death_t))
        death_fade = 1.0 - death_t
        if death_fade <= 0.01:
            return False, 0, COL_DEAD, 0
        alive = False

    # Pulse for living fittest nodes
    pulse = 1.0
    if node.is_fittest and alive and grow_t >= 1.0:
        pulse = 1.0 + 0.08 * math.sin(gen_time * 4.0)

    radius = node.max_radius * grow_t * pulse

    # Color: alive nodes keep their color, dying nodes fade to dead gray
    if alive:
        color = node.color
    else:
        color = lerp_color(node.color, COL_DEAD, 1.0 - death_fade)

    # Opacity
    opacity = grow_t * death_fade

    return True, radius * death_fade if not alive else radius, color, opacity

# ============================================================
# Connection state
# ============================================================

def connection_state(parent_node, child_node, gen_time):
    """Returns (visible, extend_t, color, opacity, radius)."""
    pvis, _, _, pop = node_state(parent_node, gen_time)
    cvis, _, _, cop = node_state(child_node, gen_time)

    if not pvis or cop <= 0.01:
        return False, 0, parent_node.color, 0, 0

    # Connection extends as child grows
    grow_t = (gen_time - child_node.appear_gen) / 0.6
    grow_t = max(0.0, min(1.0, grow_t))

    opacity = min(pop, cop) * 0.85
    is_fittest = parent_node.is_fittest and child_node.is_fittest
    radius = 0.05 if is_fittest else 0.03
    radius *= lerp(0.3, 1.0, grow_t)

    return True, grow_t, parent_node.color, opacity, radius

# ============================================================
# POV-Ray frame generator
# ============================================================

POV_HEADER = r"""#version 3.7;

global_settings {
    assumed_gamma 1.0
    radiosity {
        pretrace_start 0.08
        pretrace_end 0.02
        count 50
        nearest_count 5
        error_bound 0.8
        recursion_limit 1
        brightness 0.6
    }
}

camera {
    location <-5, 5, -12>
    look_at <10, 0.5, 3>
    angle 55
    right x * 16/9
}

light_source {
    <15, 20, -10>
    color rgb <1.0, 0.95, 0.85> * 1.2
    area_light <3, 0, 0>, <0, 0, 3>, 4, 4
    adaptive 1
    jitter
}

light_source {
    <-15, 12, -5>
    color rgb <0.4, 0.6, 0.9> * 0.5
}

light_source {
    <10, 5, 25>
    color rgb <1.0, 0.5, 0.2> * 0.35
}

sky_sphere {
    pigment {
        gradient y
        color_map {
            [0.0 color rgb <0.02, 0.03, 0.06>]
            [0.3 color rgb <0.04, 0.05, 0.10>]
            [1.0 color rgb <0.06, 0.07, 0.14>]
        }
        scale 2
        translate -1
    }
}

plane {
    y, -0.8
    pigment { color rgb <0.03, 0.04, 0.07> }
    finish {
        reflection { 0.12 }
        specular 0.1
        roughness 0.05
        ambient 0.02
    }
}
"""

def write_frame(frame_path, nodes, node_map, gen_time, brightness):
    """Write a single .pov frame."""
    with open(frame_path, 'w') as f:
        f.write(POV_HEADER)

        # Brightness overlay for fade in/out
        if brightness < 1.0:
            f.write(pov("""
// Fade overlay
plane {
    z, 100
    pigment { color rgb <0, 0, 0> transmit $br }
    finish { ambient 0 diffuse 0 }
    no_shadow
    no_reflection
}
""", br=brightness))

        # Render nodes
        for node in nodes:
            vis, radius, color, opacity = node_state(node, gen_time)
            if not vis or radius < 0.005:
                continue

            transmit = 1.0 - (opacity * brightness)
            transmit = max(0.0, min(0.98, transmit))
            col = col_str(color)
            px, py, pz = node.pos

            is_dead = node.die_gen > 0 and gen_time >= node.die_gen

            if is_dead:
                f.write(pov("""
sphere {
    <$px, $py, $pz>, $rad
    texture {
        pigment { color $col transmit $tr }
        finish { specular 0.1 roughness 0.3 ambient 0.01 diffuse 0.3 }
    }
}
""", px=px, py=py, pz=pz, rad=radius, col=col, tr=transmit))
                if opacity > 0.1:
                    ring_r = radius * 1.1
                    ring_t = radius * 0.05
                    ring_tr = 0.5 + transmit * 0.3
                    f.write(pov("""
torus {
    $rr, $rt
    rotate x * 90
    translate <$px, $py, $pz>
    pigment { color rgb <0.9, 0.2, 0.15> transmit $rtr }
    finish { ambient 0.3 diffuse 0.5 }
    no_shadow
}
""", rr=ring_r, rt=ring_t, px=px, py=py, pz=pz, rtr=ring_tr))
            else:
                bc = col_str((min(color[0]*1.4, 1.0), min(color[1]*1.4, 1.0), min(color[2]*1.4, 1.0)))
                node_tr = max(0.02, transmit * 0.3)
                f.write(pov("""
sphere {
    <$px, $py, $pz>, $rad
    texture {
        pigment { color $bc transmit $ntr }
        finish { specular 0.9 roughness 0.015 reflection { 0.12 } ambient 0.15 diffuse 0.8 }
    }
    interior { ior 1.3 }
}
""", px=px, py=py, pz=pz, rad=radius, bc=bc, ntr=node_tr))
                if node.is_fittest and opacity > 0.5:
                    halo_r = radius * 1.8
                    gc = col_str((min(color[0]*1.5, 1.0), min(color[1]*1.5, 1.0), min(color[2]*1.5, 1.0)))
                    htr = 0.82 + transmit * 0.1
                    f.write(pov("""
sphere {
    <$px, $py, $pz>, $hr
    pigment { color $gc transmit $htr }
    finish { ambient 1.0 diffuse 0.0 }
    no_shadow
}
""", px=px, py=py, pz=pz, hr=halo_r, gc=gc, htr=htr))

        # Render connections
        for node in nodes:
            if not node.parent:
                continue
            parent_node = node_map.get(node.parent)
            if not parent_node:
                continue

            vis, extend_t, color, opacity, radius = connection_state(parent_node, node, gen_time)
            if not vis or radius < 0.002 or extend_t < 0.01:
                continue

            ppx, ppy, ppz = parent_node.pos
            cx, cy, cz = node.pos
            ex = lerp(ppx, cx, extend_t)
            ey = lerp(ppy, cy, extend_t)
            ez = lerp(ppz, cz, extend_t)

            transmit = 1.0 - (opacity * brightness)
            transmit = max(0.0, min(0.95, transmit))
            bc = col_str((min(color[0]*1.8, 1.0), min(color[1]*1.8, 1.0), min(color[2]*1.8, 1.0)))
            ctr = transmit * 0.5

            f.write(pov("""
cylinder {
    <$px, $py, $pz>, <$ex, $ey, $ez>, $rad
    texture {
        pigment { color $bc transmit $ctr }
        finish { specular 0.4 roughness 0.08 ambient 0.25 diffuse 0.7 }
    }
    no_shadow
}
""", px=ppx, py=ppy, pz=ppz, ex=ex, ey=ey, ez=ez, rad=radius, bc=bc, ctr=ctr))

# ============================================================
# Main
# ============================================================

def main():
    parser = argparse.ArgumentParser(description="Generate ETIL evolution animation")
    parser.add_argument("--fps", type=int, default=FPS)
    parser.add_argument("--width", type=int, default=WIDTH)
    parser.add_argument("--height", type=int, default=HEIGHT)
    parser.add_argument("--quality", type=int, default=QUALITY)
    parser.add_argument("--render-only", action="store_true", help="Generate frames but skip ffmpeg")
    args = parser.parse_args()

    total_time = FADE_IN + (TOTAL_GENS * GEN_DURATION) + HOLD_END + FADE_OUT
    total_frames = int(total_time * args.fps)

    print(f"Animation: {total_time:.1f}s @ {args.fps}fps = {total_frames} frames")
    print(f"Resolution: {args.width}x{args.height}, Quality: {args.quality}")

    # Build tree
    nodes = build_tree()
    node_map = {n.name: n for n in nodes}

    # Create frames directory
    if os.path.exists(FRAMES_DIR):
        shutil.rmtree(FRAMES_DIR)
    os.makedirs(FRAMES_DIR)

    # Generate and render frames
    for frame_idx in range(total_frames):
        t = frame_idx / args.fps  # time in seconds

        # Compute generation time (0 = G0 appears)
        gen_time = (t - FADE_IN) / GEN_DURATION
        gen_time = max(-0.5, gen_time)

        # Compute brightness for fade in/out
        if t < FADE_IN:
            brightness = t / FADE_IN
        elif t > (total_time - FADE_OUT):
            brightness = (total_time - t) / FADE_OUT
        else:
            brightness = 1.0
        brightness = max(0.0, min(1.0, brightness))

        # Write .pov frame
        frame_name = f"frame_{frame_idx:04d}"
        pov_path = os.path.join(FRAMES_DIR, f"{frame_name}.pov")
        png_path = os.path.join(FRAMES_DIR, f"{frame_name}.png")

        write_frame(pov_path, nodes, node_map, gen_time, brightness)

        # Render with povray
        cmd = [
            "povray",
            f"+W{args.width}", f"+H{args.height}",
            f"+A{AA}", f"+Q{args.quality}",
            "-D", f"+O{png_path}", pov_path
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"ERROR rendering frame {frame_idx}: {result.stderr[-200:]}")
            sys.exit(1)

        # Progress
        pct = (frame_idx + 1) / total_frames * 100
        sys.stdout.write(f"\r  Rendered frame {frame_idx + 1}/{total_frames} ({pct:.0f}%)")
        sys.stdout.flush()

    print()

    # Clean up .pov files (keep only PNGs)
    for f in os.listdir(FRAMES_DIR):
        if f.endswith(".pov"):
            os.remove(os.path.join(FRAMES_DIR, f))

    if args.render_only:
        print(f"Frames rendered to {FRAMES_DIR}/")
        return

    # Assemble with ffmpeg
    print(f"Assembling {OUTPUT_FILE} ...")
    ffmpeg_cmd = [
        "ffmpeg", "-y",
        "-framerate", str(args.fps),
        "-i", os.path.join(FRAMES_DIR, "frame_%04d.png"),
        "-c:v", "libx264",
        "-preset", "slow",
        "-crf", "18",
        "-pix_fmt", "yuv420p",
        "-movflags", "+faststart",
        OUTPUT_FILE
    ]
    result = subprocess.run(ffmpeg_cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"ffmpeg error: {result.stderr[-500:]}")
        sys.exit(1)

    # File size
    size_mb = os.path.getsize(OUTPUT_FILE) / (1024 * 1024)
    print(f"Done: {OUTPUT_FILE} ({size_mb:.1f} MB, {total_frames} frames, {total_time:.1f}s)")

if __name__ == "__main__":
    main()
