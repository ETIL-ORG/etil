#!/usr/bin/env python3
"""
ETIL word-type diagram with rectangular category grids around central types.

Initial layout: each category forms a rectangular 2D grid, evenly spaced
around the type hexagons.  Alternating optimization then swaps words
between positions and re-centers type hexagons to minimize total edge distance.

Usage: gen_type_map_grid.py [cohesion_weight] [output_dir]
  cohesion_weight: float, default 1.2 — strength of category-cohesion force
  output_dir:      directory for .dot output, default "."
"""
import math
import os
import sys
import numpy as np
from numpy.linalg import eigh

# ── Value::Types with colors ──
TYPES = {
    "Integer":    "#00d4ff",
    "Float":      "#feca57",
    "Boolean":    "#55efc4",
    "String":     "#fd79a8",
    "Array":      "#a29bfe",
    "ByteArray":  "#ff7675",
    "Map":        "#74b9ff",
    "Json":       "#fab1a0",
    "Matrix":     "#00cec9",
    "Observable": "#e17055",
    "Xt":         "#dfe6e9",
}

# ── Categories and words ──
CATEGORIES = [
    ("arithmetic", "#00d4ff", ["+","-","*","/","mod","/mod","negate","abs","max","min","1+","1-"]),
    ("comparison", "#55efc4", ["=","<>","<",">","<=",">=","0=","0<","0>","within"]),
    ("logic", "#ffeaa7", ["true","false","not","and","or","xor","invert","lshift","rshift","lroll","rroll"]),
    ("stack", "#636e72", ["dup","drop","swap","over","rot","pick","nip","tuck","depth","?dup","roll","-rot"]),
    ("i/o", "#dfe6e9", [".","cr","emit","space","spaces","words","hex.","bin.","oct.",".|"]),
    ("math", "#feca57", ["sqrt","sin","cos","tan","tanh","asin","acos","atan","atan2","log","log2","log10",
                          "exp","pow","ceil","floor","round","trunc","fmin","fmax","pi","f~",
                          "random","random-seed","random-range"]),
    ("conversion", "#81ecec", ["bool","int->float","float->int","number->string","string->number"]),
    ("string", "#fd79a8", ["type","s+","s=","s<>","slength","substr","strim","sfind","sreplace",
                            "ssplit","sjoin","sregex-find","sregex-replace","sregex-search","sregex-match",
                            "sprintf","staint","s>upper","s>lower","s|"]),
    ("array", "#a29bfe", ["array-new","array-push","array-pop","array-get","array-set","array-length",
                           "array-shift","array-unshift","array-compact","array-reverse","array-sort",
                           "array-each","array-map","array-filter","array-reduce"]),
    ("byte-array", "#ff7675", ["bytes-new","bytes-get","bytes-set","bytes-length","bytes-resize",
                                "bytes->string","string->bytes"]),
    ("map", "#74b9ff", ["map-new","map-set","map-get","map-remove","map-length","map-keys",
                         "map-values","map-has?"]),
    ("json", "#fab1a0", ["json-parse","json-dump","json-pretty","json-get","json-length","json-type",
                          "json-keys","json->map","json->array","map->json","array->json","json->value","j|"]),
    ("matrix", "#00cec9", ["mat-new","mat-eye","array->mat","mat-diag","mat-rand","mat-get","mat-set",
                            "mat-rows","mat-cols","mat-row","mat-col","mat*","mat+","mat-","mat-scale",
                            "mat-transpose","mat-solve","mat-inv","mat-det","mat-eigen","mat-svd",
                            "mat-lstsq","mat-norm","mat-trace","mat.","mat-relu","mat-sigmoid","mat-tanh",
                            "mat-relu'","mat-sigmoid'","mat-tanh'","mat-hadamard","mat-add-col","mat-clip",
                            "mat-randn","mat-sum","mat-col-sum","mat-mean","mat-softmax",
                            "mat-cross-entropy","mat-apply","mat->array","mat->json","json->mat"]),
    ("observable", "#e17055", ["obs-from","obs-of","obs-empty","obs-range","obs-map","obs-map-with",
                                "obs-filter","obs-filter-with","obs-scan","obs-reduce","obs-take","obs-skip",
                                "obs-distinct","obs-merge","obs-concat","obs-zip","obs-subscribe",
                                "obs-to-array","obs-count","obs?","obs-kind","obs-timer","obs-interval",
                                "obs-delay","obs-timestamp","obs-debounce-time","obs-throttle-time",
                                "obs-sample-time","obs-timeout","obs-audit-time","obs-buffer-time",
                                "obs-time-interval","obs-take-until-time","obs-delay-each","obs-retry-delay",
                                "obs-buffer","obs-buffer-when","obs-window","obs-flat-map","obs-to-string",
                                "obs-read-bytes","obs-read-lines","obs-read-json","obs-read-csv",
                                "obs-readdir","obs-write-file","obs-append-file","obs-http-get",
                                "obs-http-post","obs-http-sse","obs-tap","obs-pairwise","obs-first",
                                "obs-last","obs-take-while","obs-distinct-until","obs-start-with",
                                "obs-finalize","obs-switch-map","obs-catch"]),
    ("execution", "#b2bec3", ["'","execute","xt?",">name","xt-body"]),
    ("memory", "#c8d6e5", ["create",",","@","!","allot","variable","constant","immediate"]),
    ("time", "#ffeaa7", ["time-us","us->iso","us->iso-us","time-iso","time-iso-us",
                          "us->jd","jd->us","us->mjd","mjd->us","time-jd","time-mjd","sleep","elapsed"]),
    ("system", "#636e72", ["sys-semver","sys-timestamp","sys-datafields","sys-notification",
                            "user-notification","abort"]),
    ("lvfs", "#a29bfe", ["cwd","cd","ls","ll","lr","cat"]),
    ("http", "#ff7675", ["http-get","http-post"]),
    ("mongodb", "#fab1a0", ["mongo-find","mongo-count","mongo-insert","mongo-update","mongo-delete"]),
    ("control", "#dfe6e9", ["if","else","then","do","loop","+loop","i","j","begin","until",
                             "while","repeat","again",">r","r>","r@","leave","exit","recurse"]),
    ("dictionary", "#636e72", ["dict-forget","dict-forget-all","file-load","include","library",
                                "evaluate","marker","marker-restore","forget","forget-all"]),
    ("debug", "#b2bec3", ["s.","dump","see",".s"]),
]

# ── Type connections ──
TYPE_LINKS = {
    "+": (["Integer","Float"], ["Integer","Float"]),
    "-": (["Integer","Float"], ["Integer","Float"]),
    "*": (["Integer","Float"], ["Integer","Float"]),
    "/": (["Integer","Float"], ["Integer","Float"]),
    "mod": (["Integer"], ["Integer"]),
    "/mod": (["Integer"], ["Integer"]),
    "negate": (["Integer","Float"], ["Integer","Float"]),
    "abs": (["Integer","Float"], ["Integer","Float"]),
    "max": (["Integer","Float"], ["Integer","Float"]),
    "min": (["Integer","Float"], ["Integer","Float"]),
    "1+": (["Integer"], ["Integer"]),
    "1-": (["Integer"], ["Integer"]),
    "=": (["Integer","Float","String"], ["Boolean"]),
    "<>": (["Integer","Float","String"], ["Boolean"]),
    "<": (["Integer","Float"], ["Boolean"]),
    ">": (["Integer","Float"], ["Boolean"]),
    "<=": (["Integer","Float"], ["Boolean"]),
    ">=": (["Integer","Float"], ["Boolean"]),
    "0=": (["Integer"], ["Boolean"]),
    "0<": (["Integer"], ["Boolean"]),
    "0>": (["Integer"], ["Boolean"]),
    "within": (["Integer"], ["Boolean"]),
    "true": ([], ["Boolean"]),
    "false": ([], ["Boolean"]),
    "not": (["Boolean"], ["Boolean"]),
    "and": (["Boolean","Integer"], ["Boolean","Integer"]),
    "or": (["Boolean","Integer"], ["Boolean","Integer"]),
    "xor": (["Boolean","Integer"], ["Boolean","Integer"]),
    "invert": (["Boolean","Integer"], ["Boolean","Integer"]),
    "lshift": (["Integer"], ["Integer"]),
    "rshift": (["Integer"], ["Integer"]),
    "lroll": (["Integer"], ["Integer"]),
    "rroll": (["Integer"], ["Integer"]),
    "depth": ([], ["Integer"]),
    ".": (["Integer","Float","String","Boolean"], []),
    "emit": (["Integer"], []),
    "hex.": (["Integer"], []),
    "bin.": (["Integer"], []),
    "oct.": (["Integer"], []),
    "sqrt": (["Float"], ["Float"]),
    "sin": (["Float"], ["Float"]),
    "cos": (["Float"], ["Float"]),
    "tan": (["Float"], ["Float"]),
    "tanh": (["Float"], ["Float"]),
    "asin": (["Float"], ["Float"]),
    "acos": (["Float"], ["Float"]),
    "atan": (["Float"], ["Float"]),
    "atan2": (["Float"], ["Float"]),
    "log": (["Float"], ["Float"]),
    "log2": (["Float"], ["Float"]),
    "log10": (["Float"], ["Float"]),
    "exp": (["Float"], ["Float"]),
    "pow": (["Float"], ["Float"]),
    "ceil": (["Float"], ["Float"]),
    "floor": (["Float"], ["Float"]),
    "round": (["Float"], ["Float"]),
    "trunc": (["Float"], ["Float"]),
    "fmin": (["Float"], ["Float"]),
    "fmax": (["Float"], ["Float"]),
    "pi": ([], ["Float"]),
    "f~": (["Float"], ["Boolean"]),
    "random": ([], ["Float"]),
    "random-seed": (["Integer"], []),
    "random-range": (["Integer"], ["Integer"]),
    "bool": ([], ["Boolean"]),
    "int->float": (["Integer"], ["Float"]),
    "float->int": (["Float"], ["Integer"]),
    "number->string": (["Integer","Float"], ["String"]),
    "string->number": (["String"], ["Integer","Float"]),
    "type": (["String"], []),
    "s+": (["String"], ["String"]),
    "s=": (["String"], ["Boolean"]),
    "s<>": (["String"], ["Boolean"]),
    "slength": (["String"], ["Integer"]),
    "substr": (["String","Integer"], ["String"]),
    "strim": (["String"], ["String"]),
    "sfind": (["String"], ["Integer"]),
    "sreplace": (["String"], ["String"]),
    "ssplit": (["String"], ["Array"]),
    "sjoin": (["Array","String"], ["String"]),
    "sregex-find": (["String"], ["String"]),
    "sregex-replace": (["String"], ["String"]),
    "sregex-search": (["String"], ["Boolean"]),
    "sregex-match": (["String"], ["Boolean"]),
    "sprintf": (["String"], ["String"]),
    "staint": (["String"], ["Boolean"]),
    "s>upper": (["String"], ["String"]),
    "s>lower": (["String"], ["String"]),
    "array-new": ([], ["Array"]),
    "array-push": (["Array"], ["Array"]),
    "array-pop": (["Array"], ["Array"]),
    "array-get": (["Array","Integer"], []),
    "array-set": (["Array","Integer"], ["Array"]),
    "array-length": (["Array"], ["Integer"]),
    "array-shift": (["Array"], ["Array"]),
    "array-unshift": (["Array"], ["Array"]),
    "array-compact": (["Array"], ["Array"]),
    "array-reverse": (["Array"], ["Array"]),
    "array-sort": (["Array"], ["Array"]),
    "array-each": (["Array","Xt"], []),
    "array-map": (["Array","Xt"], ["Array"]),
    "array-filter": (["Array","Xt"], ["Array"]),
    "array-reduce": (["Array","Xt"], []),
    "bytes-new": (["Integer"], ["ByteArray"]),
    "bytes-get": (["ByteArray","Integer"], ["Integer"]),
    "bytes-set": (["ByteArray","Integer"], ["ByteArray"]),
    "bytes-length": (["ByteArray"], ["Integer"]),
    "bytes-resize": (["ByteArray","Integer"], ["ByteArray"]),
    "bytes->string": (["ByteArray"], ["String"]),
    "string->bytes": (["String"], ["ByteArray"]),
    "map-new": ([], ["Map"]),
    "map-set": (["Map","String"], ["Map"]),
    "map-get": (["Map","String"], []),
    "map-remove": (["Map","String"], ["Map"]),
    "map-length": (["Map"], ["Integer"]),
    "map-keys": (["Map"], ["Array"]),
    "map-values": (["Map"], ["Array"]),
    "map-has?": (["Map","String"], ["Boolean"]),
    "json-parse": (["String"], ["Json"]),
    "json-dump": (["Json"], ["String"]),
    "json-pretty": (["Json"], ["String"]),
    "json-get": (["Json","String"], []),
    "json-length": (["Json"], ["Integer"]),
    "json-type": (["Json"], ["String"]),
    "json-keys": (["Json"], ["Array"]),
    "json->map": (["Json"], ["Map"]),
    "json->array": (["Json"], ["Array"]),
    "map->json": (["Map"], ["Json"]),
    "array->json": (["Array"], ["Json"]),
    "json->value": (["Json"], []),
    "mat-new": (["Integer"], ["Matrix"]),
    "mat-eye": (["Integer"], ["Matrix"]),
    "array->mat": (["Array"], ["Matrix"]),
    "mat-diag": (["Array"], ["Matrix"]),
    "mat-rand": (["Integer"], ["Matrix"]),
    "mat-get": (["Matrix","Integer"], ["Float"]),
    "mat-set": (["Matrix","Integer","Float"], ["Matrix"]),
    "mat-rows": (["Matrix"], ["Integer"]),
    "mat-cols": (["Matrix"], ["Integer"]),
    "mat-row": (["Matrix","Integer"], ["Array"]),
    "mat-col": (["Matrix","Integer"], ["Array"]),
    "mat*": (["Matrix"], ["Matrix"]),
    "mat+": (["Matrix"], ["Matrix"]),
    "mat-": (["Matrix"], ["Matrix"]),
    "mat-scale": (["Matrix","Float"], ["Matrix"]),
    "mat-transpose": (["Matrix"], ["Matrix"]),
    "mat-solve": (["Matrix"], ["Matrix","Boolean"]),
    "mat-inv": (["Matrix"], ["Matrix","Boolean"]),
    "mat-det": (["Matrix"], ["Float","Boolean"]),
    "mat-eigen": (["Matrix"], ["Matrix","Boolean"]),
    "mat-svd": (["Matrix"], ["Matrix","Boolean"]),
    "mat-lstsq": (["Matrix"], ["Matrix","Boolean"]),
    "mat-norm": (["Matrix"], ["Float"]),
    "mat-trace": (["Matrix"], ["Float"]),
    "mat.": (["Matrix"], []),
    "mat-relu": (["Matrix"], ["Matrix"]),
    "mat-sigmoid": (["Matrix"], ["Matrix"]),
    "mat-tanh": (["Matrix"], ["Matrix"]),
    "mat-relu'": (["Matrix"], ["Matrix"]),
    "mat-sigmoid'": (["Matrix"], ["Matrix"]),
    "mat-tanh'": (["Matrix"], ["Matrix"]),
    "mat-hadamard": (["Matrix"], ["Matrix"]),
    "mat-add-col": (["Matrix"], ["Matrix"]),
    "mat-clip": (["Matrix","Float"], ["Matrix"]),
    "mat-randn": (["Integer"], ["Matrix"]),
    "mat-sum": (["Matrix"], ["Float"]),
    "mat-col-sum": (["Matrix"], ["Matrix"]),
    "mat-mean": (["Matrix"], ["Float"]),
    "mat-softmax": (["Matrix"], ["Matrix"]),
    "mat-cross-entropy": (["Matrix"], ["Float"]),
    "mat-apply": (["Matrix","Xt"], ["Matrix"]),
    "mat->array": (["Matrix"], ["Array"]),
    "mat->json": (["Matrix"], ["Json"]),
    "json->mat": (["Json"], ["Matrix"]),
    "obs-from": (["Array"], ["Observable"]),
    "obs-of": ([], ["Observable"]),
    "obs-empty": ([], ["Observable"]),
    "obs-range": (["Integer"], ["Observable"]),
    "obs-map": (["Observable","Xt"], ["Observable"]),
    "obs-map-with": (["Observable","Xt"], ["Observable"]),
    "obs-filter": (["Observable","Xt"], ["Observable"]),
    "obs-filter-with": (["Observable","Xt"], ["Observable"]),
    "obs-scan": (["Observable","Xt"], ["Observable"]),
    "obs-reduce": (["Observable","Xt"], []),
    "obs-take": (["Observable","Integer"], ["Observable"]),
    "obs-skip": (["Observable","Integer"], ["Observable"]),
    "obs-distinct": (["Observable"], ["Observable"]),
    "obs-merge": (["Observable"], ["Observable"]),
    "obs-concat": (["Observable"], ["Observable"]),
    "obs-zip": (["Observable"], ["Observable"]),
    "obs-subscribe": (["Observable","Xt"], []),
    "obs-to-array": (["Observable"], ["Array"]),
    "obs-count": (["Observable"], ["Integer"]),
    "obs?": ([], ["Boolean"]),
    "obs-kind": (["Observable"], ["String"]),
    "obs-timer": (["Integer"], ["Observable"]),
    "obs-interval": (["Integer"], ["Observable"]),
    "obs-delay": (["Observable","Integer"], ["Observable"]),
    "obs-timestamp": (["Observable"], ["Observable"]),
    "obs-debounce-time": (["Observable","Integer"], ["Observable"]),
    "obs-throttle-time": (["Observable","Integer"], ["Observable"]),
    "obs-sample-time": (["Observable","Integer"], ["Observable"]),
    "obs-timeout": (["Observable","Integer"], ["Observable"]),
    "obs-audit-time": (["Observable","Integer"], ["Observable"]),
    "obs-buffer-time": (["Observable","Integer"], ["Observable"]),
    "obs-time-interval": (["Observable"], ["Observable"]),
    "obs-take-until-time": (["Observable","Integer"], ["Observable"]),
    "obs-delay-each": (["Observable","Integer"], ["Observable"]),
    "obs-retry-delay": (["Observable","Integer","Xt"], ["Observable"]),
    "obs-buffer": (["Observable","Integer"], ["Observable"]),
    "obs-buffer-when": (["Observable","Xt"], ["Observable"]),
    "obs-window": (["Observable","Integer"], ["Observable"]),
    "obs-flat-map": (["Observable","Xt"], ["Observable"]),
    "obs-to-string": (["Observable"], ["String"]),
    "obs-read-bytes": (["String"], ["Observable"]),
    "obs-read-lines": (["String"], ["Observable"]),
    "obs-read-json": (["String"], ["Observable"]),
    "obs-read-csv": (["String"], ["Observable"]),
    "obs-readdir": (["String"], ["Observable"]),
    "obs-write-file": (["Observable","String"], ["Observable"]),
    "obs-append-file": (["Observable","String"], ["Observable"]),
    "obs-http-get": (["String","Map"], ["Observable"]),
    "obs-http-post": (["String","Map"], ["Observable"]),
    "obs-http-sse": (["String","Map"], ["Observable"]),
    "obs-tap": (["Observable","Xt"], ["Observable"]),
    "obs-pairwise": (["Observable"], ["Observable"]),
    "obs-first": (["Observable"], ["Observable"]),
    "obs-last": (["Observable"], ["Observable"]),
    "obs-take-while": (["Observable","Xt"], ["Observable"]),
    "obs-distinct-until": (["Observable"], ["Observable"]),
    "obs-start-with": (["Observable"], ["Observable"]),
    "obs-finalize": (["Observable","Xt"], ["Observable"]),
    "obs-switch-map": (["Observable","Xt"], ["Observable"]),
    "obs-catch": (["Observable","Xt"], ["Observable"]),
    "'": ([], ["Xt"]),
    "execute": (["Xt"], []),
    "xt?": ([], ["Boolean"]),
    ">name": (["Xt"], ["String"]),
    "xt-body": (["Xt"], ["Array"]),
    "time-us": ([], ["Integer"]),
    "us->iso": (["Integer"], ["String"]),
    "us->iso-us": (["Integer"], ["String"]),
    "us->jd": (["Integer"], ["Float"]),
    "jd->us": (["Float"], ["Integer"]),
    "us->mjd": (["Integer"], ["Float"]),
    "mjd->us": (["Float"], ["Integer"]),
    "elapsed": (["Xt"], ["Integer"]),
    "sleep": (["Integer"], []),
    "http-get": (["String","Map"], ["ByteArray","Integer","Boolean"]),
    "http-post": (["String","Map","ByteArray"], ["ByteArray","Integer","Boolean"]),
    "mongo-find": (["String","Json"], ["Json","Boolean"]),
    "mongo-count": (["String","Json"], ["Integer","Boolean"]),
    "mongo-insert": (["String","Json"], ["Boolean"]),
    "mongo-update": (["String","Json"], ["Boolean"]),
    "mongo-delete": (["String","Json"], ["Boolean"]),
    "if": (["Boolean"], []),
    "until": (["Boolean"], []),
    "while": (["Boolean"], []),
    "i": ([], ["Integer"]),
    "j": ([], ["Integer"]),
    "evaluate": (["String"], []),
    "cat": (["String"], []),
    "cd": (["String"], []),
}


def sanitize(name):
    return "w_" + name.replace("+","Plus").replace("-","Dash").replace("*","Star") \
                      .replace("/","Slash").replace(".","Dot").replace("?","Q") \
                      .replace("!","Bang").replace("@","At").replace(",","Comma") \
                      .replace("'","Tick").replace(">","Gt").replace("<","Lt") \
                      .replace("=","Eq").replace("|","Pipe").replace("~","Tilde") \
                      .replace(";","Semi").replace(":","Colon")

def escape_label(name):
    return name.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;")


def make_grid_positions(words, center_x, center_y, cell_w=2.8, cell_h=1.0):
    """Lay out words in a rectangular grid centered at (center_x, center_y)."""
    n = len(words)
    cols = max(1, int(math.ceil(math.sqrt(n * 1.5))))  # wider than tall
    rows = max(1, int(math.ceil(n / cols)))
    positions = []
    for idx in range(n):
        r, c = divmod(idx, cols)
        x = center_x + (c - (cols - 1) / 2.0) * cell_w
        y = center_y - (r - (rows - 1) / 2.0) * cell_h
        positions.append((x, y))
    grid_w = cols * cell_w
    grid_h = rows * cell_h
    return positions, grid_w, grid_h


COHESION_WEIGHT = float(sys.argv[1]) if len(sys.argv) > 1 else 1.2

# Populated in main() — maps word → (cat_center_x, cat_center_y)
word_cat_center = {}

def word_cost(word, wx, wy, type_positions):
    total = 0.0
    # Type connection distance
    if word in TYPE_LINKS:
        inputs, outputs = TYPE_LINKS[word]
        for tname in inputs + outputs:
            if tname in type_positions:
                tx, ty = type_positions[tname]
                total += math.sqrt((wx - tx)**2 + (wy - ty)**2)
    # Category cohesion: penalize distance from category center
    if word in word_cat_center:
        cx, cy = word_cat_center[word]
        total += COHESION_WEIGHT * math.sqrt((wx - cx)**2 + (wy - cy)**2)
    return total


def compute_total_distance(word_positions, type_positions):
    total = 0.0
    count = 0
    for word, (wx, wy) in word_positions.items():
        if word not in TYPE_LINKS:
            continue
        inputs, outputs = TYPE_LINKS[word]
        for tname in inputs + outputs:
            if tname in type_positions:
                tx, ty = type_positions[tname]
                total += math.sqrt((wx - tx)**2 + (wy - ty)**2)
                count += 1
    return total, count


def optimize_type_positions(word_positions, type_names):
    """Place each type at weighted centroid of connected words, with repulsion."""
    positions = {}
    for ti, tname in enumerate(type_names):
        xs, ys = [], []
        for word, (wx, wy) in word_positions.items():
            if word in TYPE_LINKS:
                inputs, outputs = TYPE_LINKS[word]
                weight = (inputs + outputs).count(tname)
                if weight > 0:
                    xs.extend([wx] * weight)
                    ys.extend([wy] * weight)
        if xs:
            cx, cy = np.mean(xs), np.mean(ys)
            # Pull 60% toward origin
            cx *= 0.40
            cy *= 0.40
        else:
            angle = 2 * math.pi * ti / len(type_names)
            cx = 4.0 * math.cos(angle)
            cy = 4.0 * math.sin(angle)
        # Clamp radius
        dist = math.sqrt(cx**2 + cy**2)
        min_r, max_r = 3.0, 10.0
        if dist < min_r and dist > 0.01:
            cx *= min_r / dist
            cy *= min_r / dist
        elif dist > max_r:
            cx *= max_r / dist
            cy *= max_r / dist
        positions[tname] = (cx, cy)

    # Repulsion
    names = list(positions.keys())
    for _ in range(80):
        for i in range(len(names)):
            for j in range(i + 1, len(names)):
                ax, ay = positions[names[i]]
                bx, by = positions[names[j]]
                dx, dy = bx - ax, by - ay
                d = math.sqrt(dx**2 + dy**2)
                sep = 3.0
                if d < sep and d > 0.01:
                    push = (sep - d) / 2.0
                    nx, ny = dx / d, dy / d
                    positions[names[i]] = (ax - nx * push, ay - ny * push)
                    positions[names[j]] = (bx + nx * push, by + ny * push)
    return positions


def swap_optimization(slots, word_at, type_positions, max_rounds=40):
    """
    Greedy swap: try swapping pairs of words between any two slots.
    Use random sampling for efficiency (330^2 = 108K pairs per round is fine).
    """
    n = len(slots)

    def cost_of(word, slot_idx):
        sx, sy = slots[slot_idx]
        return word_cost(word, sx, sy, type_positions)

    current_cost = sum(cost_of(word_at[i], i) for i in range(n))
    print(f"  Swap starting cost: {current_cost:.0f}")

    rng = np.random.default_rng(42)

    for rnd in range(max_rounds):
        improved = False
        indices = list(range(n))
        rng.shuffle(indices)
        for i in indices:
            wi = word_at[i]
            ci = cost_of(wi, i)
            candidates = rng.choice(n, size=min(60, n), replace=False)
            for j in candidates:
                if i == j:
                    continue
                wj = word_at[j]
                cj = cost_of(wj, j)
                old = ci + cj
                new = cost_of(wi, j) + cost_of(wj, i)
                if new < old - 0.01:
                    word_at[i], word_at[j] = wj, wi
                    wi = wj
                    ci = cost_of(wi, i)
                    current_cost += (new - old)
                    improved = True

        if not improved:
            print(f"  Converged after {rnd + 1} rounds")
            break
        if rnd % 5 == 0 or rnd == max_rounds - 1:
            print(f"  Round {rnd + 1}: cost = {current_cost:.0f}")

    return word_at, current_cost


def main():
    output_dir = sys.argv[2] if len(sys.argv) > 2 else "."

    n_cats = len(CATEGORIES)
    total_words = sum(len(w) for _, _, w in CATEGORIES)
    print(f"{total_words} words across {n_cats} categories (cohesion={COHESION_WEIGHT:.2f})")

    # ── Step 1: Initial grid layout — categories around the center ──
    ORBIT_RADIUS = 22.0
    slots = []        # (x, y) for each word position
    word_at = []      # word name at each slot
    word_meta = {}    # word → (cat_name, cat_color)
    cat_label_pos = {}  # cat_name → (x, y) for label

    for ci, (cat_name, cat_color, words) in enumerate(CATEGORIES):
        angle = 2 * math.pi * ci / n_cats - math.pi / 2
        cx = ORBIT_RADIUS * math.cos(angle)
        cy = ORBIT_RADIUS * math.sin(angle)
        positions, gw, gh = make_grid_positions(words, cx, cy)
        cat_label_pos[cat_name] = (cx, cy)
        for wi, word in enumerate(words):
            slots.append(positions[wi])
            word_at.append(word)
            word_meta[word] = (cat_name, cat_color)
            word_cat_center[word] = (cx, cy)  # cohesion anchor

    slots = list(slots)
    type_names = list(TYPES.keys())

    # ── Step 2: Initial type positions ──
    wp = {word_at[i]: slots[i] for i in range(len(slots))}
    type_pos = optimize_type_positions(wp, type_names)
    dist, cnt = compute_total_distance(wp, type_pos)
    print(f"Initial: distance={dist:.0f} ({cnt} edges)")

    # ── Step 3: Alternating optimization ──
    for it in range(1, 9):
        print(f"\n--- Iteration {it} ---")
        word_at, _ = swap_optimization(slots, word_at, type_pos, max_rounds=40)
        wp = {word_at[i]: slots[i] for i in range(len(slots))}
        type_pos = optimize_type_positions(wp, type_names)
        dist, cnt = compute_total_distance(wp, type_pos)
        print(f"  After iteration {it}: distance={dist:.0f}")

    # Final positions
    word_positions = {word_at[i]: slots[i] for i in range(len(slots))}
    total_dist, edge_count = compute_total_distance(word_positions, type_pos)
    print(f"\nFinal: {total_dist:.0f} total distance across {edge_count} edges")
    print(f"Mean edge length: {total_dist / max(edge_count, 1):.1f}")

    # ── Step 4: Generate dot file ──
    lines = []
    lines.append('digraph ETIL_TypeGrid {')
    lines.append('  graph [size="60,60!" ratio=fill dpi=100 bgcolor="#1a1a2e"')
    lines.append('         pad="2.0" overlap=false splines=true')
    lines.append('         label=<<FONT FACE="Helvetica Neue" POINT-SIZE="42" COLOR="#e0e0e0">')
    lines.append(f'         ETIL v0.9.9 — Word / Type Map ({total_words} words, {len(type_names)} types, grid layout)</FONT>>')
    lines.append('         labelloc=t labeljust=c];')
    lines.append('  node [fontname="Helvetica Neue" fontcolor="#e0e0e0"];')
    lines.append('  edge [penwidth=0.7];')
    lines.append('')

    # Type hexagons
    for tname, tcolor in TYPES.items():
        tx, ty = type_pos[tname]
        tid = f"T_{tname}"
        lines.append(f'  {tid} [')
        lines.append(f'    label=<<FONT POINT-SIZE="18"><B>{tname}</B></FONT>>')
        lines.append(f'    shape=hexagon style=filled fillcolor="{tcolor}" fontcolor="#1a1a2e"')
        lines.append(f'    width=2.2 height=1.8 fixedsize=true penwidth=2')
        lines.append(f'    pos="{tx:.4f},{ty:.4f}!"')
        lines.append(f'  ];')
    lines.append('')

    # Category labels
    cat_color_map = {c: col for c, col, _ in CATEGORIES}
    for cat_name, (cx, cy) in cat_label_pos.items():
        ccolor = cat_color_map.get(cat_name, "#888888")
        cid = f"C_{cat_name.replace('-', '_').replace('/', '_')}"
        d = math.sqrt(cx**2 + cy**2)
        if d > 0.01:
            lx = cx + (cx / d) * 3.5
            ly = cy + (cy / d) * 3.5
        else:
            lx, ly = cx, cy + 3.5
        lines.append(f'  {cid} [')
        lines.append(f'    label=<<FONT POINT-SIZE="16" COLOR="{ccolor}"><B>{escape_label(cat_name)}</B></FONT>>')
        lines.append(f'    shape=none width=0 height=0')
        lines.append(f'    pos="{lx:.4f},{ly:.4f}!"')
        lines.append(f'  ];')
    lines.append('')

    # Word rectangles
    word_ids = {}
    for i in range(len(slots)):
        word = word_at[i]
        wx, wy = slots[i]
        cat_name, cat_color = word_meta[word]
        wid = sanitize(word) + f"_{i}"
        word_ids[word] = wid
        lines.append(f'  {wid} [')
        lines.append(f'    label=<<FONT POINT-SIZE="9">{escape_label(word)}</FONT>>')
        lines.append(f'    shape=box style="filled,rounded" fillcolor="#222244"')
        lines.append(f'    color="{cat_color}" penwidth=1.5 margin="0.06,0.03"')
        lines.append(f'    pos="{wx:.4f},{wy:.4f}!"')
        lines.append(f'  ];')
    lines.append('')

    # Type edges
    lines.append('  // Input: Type -> Word (dotted)')
    for word, wid in word_ids.items():
        if word not in TYPE_LINKS:
            continue
        inputs, _ = TYPE_LINKS[word]
        for tname in inputs:
            tcolor = TYPES.get(tname, "#888888")
            tid = f"T_{tname}"
            lines.append(f'  {tid} -> {wid} [color="{tcolor}50" arrowsize=0.35 style=dotted penwidth=1.4];')

    lines.append('')
    lines.append('  // Output: Word -> Type (dashed)')
    for word, wid in word_ids.items():
        if word not in TYPE_LINKS:
            continue
        _, outputs = TYPE_LINKS[word]
        for tname in outputs:
            tcolor = TYPES.get(tname, "#888888")
            tid = f"T_{tname}"
            lines.append(f'  {wid} -> {tid} [color="{tcolor}50" arrowsize=0.35 style=dashed penwidth=1.4];')

    lines.append('}')

    dot_path = os.path.join(output_dir, f'etil-type-grid-{COHESION_WEIGHT:.2f}.dot')
    with open(dot_path, 'w') as f:
        f.write('\n'.join(lines))
    print(f"Wrote {dot_path}")


if __name__ == '__main__':
    main()
