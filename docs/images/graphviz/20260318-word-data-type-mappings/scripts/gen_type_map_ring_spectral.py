#!/usr/bin/env python3
"""
ETIL word-type ring diagram with optimized layout.

Uses spectral ordering (Fiedler vector of the graph Laplacian) to arrange
words around the ring so that words sharing type connections cluster together.
Then places type hexagons at the weighted centroid of their connected words.
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


def build_type_vector(word):
    """Build a feature vector: one dimension per type, +1 for each connection."""
    type_names = list(TYPES.keys())
    vec = np.zeros(len(type_names))
    if word in TYPE_LINKS:
        inputs, outputs = TYPE_LINKS[word]
        for t in inputs + outputs:
            if t in type_names:
                vec[type_names.index(t)] += 1.0
    return vec


def spectral_order(words_with_meta):
    """
    Spectral ordering via the Fiedler vector of the word-word affinity graph.

    Two words are "similar" if they share type connections.  The affinity
    matrix A[i,j] = dot(type_vec_i, type_vec_j).  The graph Laplacian
    L = D - A.  The eigenvector for the second-smallest eigenvalue (Fiedler
    vector) gives a 1-D embedding that minimises sum of squared distances
    between connected nodes — i.e., words with similar type signatures end
    up close together on the ring.
    """
    n = len(words_with_meta)
    print(f"Building {n}x{n} affinity matrix...")

    # Build type feature vectors
    vecs = np.array([build_type_vector(w) for w, _, _ in words_with_meta])

    # Affinity matrix: cosine-like similarity (dot product of normalised vectors)
    norms = np.linalg.norm(vecs, axis=1, keepdims=True)
    norms[norms == 0] = 1.0  # avoid div-by-zero for unconnected words
    normed = vecs / norms
    A = normed @ normed.T
    np.fill_diagonal(A, 0.0)

    # Also add a category-cohesion term so words in the same category
    # prefer to stay together (weight = 0.3)
    cat_indices = {}
    for i, (_, cat, _) in enumerate(words_with_meta):
        cat_indices.setdefault(cat, []).append(i)
    for indices in cat_indices.values():
        for a in indices:
            for b in indices:
                if a != b:
                    A[a, b] += 0.3

    # Graph Laplacian: L = D - A
    D = np.diag(A.sum(axis=1))
    L = D - A

    print("Computing eigenvectors of graph Laplacian...")
    eigenvalues, eigenvectors = eigh(L)

    # Fiedler vector = eigenvector for 2nd smallest eigenvalue
    fiedler = eigenvectors[:, 1]

    # Sort by Fiedler value → optimal 1-D ordering
    order = np.argsort(fiedler)
    return order


def optimize_type_positions(word_positions, type_names):
    """
    Place each type hexagon at the weighted centroid of its connected words.
    Then pull toward center and enforce a minimum radius for readability.
    """
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
            # Pull centroid toward origin by 70% (keep types in center region)
            cx *= 0.30
            cy *= 0.30
        else:
            # Fallback: evenly space unconnected types
            angle = 2 * math.pi * ti / len(type_names)
            cx = 4.0 * math.cos(angle)
            cy = 4.0 * math.sin(angle)

        # Enforce minimum distance from center for readability
        dist = math.sqrt(cx**2 + cy**2)
        min_r, max_r = 3.0, 9.0
        if dist < min_r:
            scale = min_r / max(dist, 0.01)
            cx *= scale
            cy *= scale
        elif dist > max_r:
            scale = max_r / dist
            cx *= scale
            cy *= scale

        positions[tname] = (cx, cy)

    # Repulsion pass: push overlapping hexagons apart
    names = list(positions.keys())
    for _ in range(50):
        for i in range(len(names)):
            for j in range(i+1, len(names)):
                ax, ay = positions[names[i]]
                bx, by = positions[names[j]]
                dx, dy = bx - ax, by - ay
                dist = math.sqrt(dx**2 + dy**2)
                min_sep = 2.8
                if dist < min_sep and dist > 0.01:
                    push = (min_sep - dist) / 2.0
                    nx, ny = dx/dist, dy/dist
                    positions[names[i]] = (ax - nx*push, ay - ny*push)
                    positions[names[j]] = (bx + nx*push, by + ny*push)

    return positions


def compute_total_distance(word_positions, type_positions):
    """Compute total Euclidean distance of all type-connection edges."""
    total = 0.0
    count = 0
    for word, (wx, wy) in word_positions.items():
        if word not in TYPE_LINKS:
            continue
        inputs, outputs = TYPE_LINKS[word]
        for tname in inputs + outputs:
            if tname in type_positions:
                tx, ty = type_positions[tname]
                total += math.sqrt((wx-tx)**2 + (wy-ty)**2)
                count += 1
    return total, count


def word_cost(word, wx, wy, type_positions):
    """Sum of distances from one word to all its connected type hexagons."""
    if word not in TYPE_LINKS:
        return 0.0
    inputs, outputs = TYPE_LINKS[word]
    total = 0.0
    for tname in inputs + outputs:
        if tname in type_positions:
            tx, ty = type_positions[tname]
            total += math.sqrt((wx - tx)**2 + (wy - ty)**2)
    return total


def local_swap_refinement(ordered_words, ring_radius, type_positions, max_rounds=30):
    """
    Greedy pairwise swap refinement on the circular word arrangement.

    For each round, try all adjacent swaps and all skip-1 swaps.
    Accept a swap if it reduces total edge distance.  Stop when a
    full round produces no improvement.

    Returns the refined ordering.
    """
    n = len(ordered_words)

    def positions_from_order(order):
        pos = {}
        for i, (word, cat, color) in enumerate(order):
            angle = 2 * math.pi * i / n - math.pi / 2
            pos[word] = (ring_radius * math.cos(angle),
                         ring_radius * math.sin(angle))
        return pos

    def total_cost(order, tpos):
        wp = positions_from_order(order)
        c = 0.0
        for word, (wx, wy) in wp.items():
            c += word_cost(word, wx, wy, tpos)
        return c

    best = list(ordered_words)
    best_cost = total_cost(best, type_positions)
    print(f"  Local swap starting cost: {best_cost:.0f}")

    for rnd in range(max_rounds):
        improved = False
        # Try swapping each pair (i, j) where j in {i+1, i+2}
        for gap in (1, 2, 3):
            for i in range(n):
                j = (i + gap) % n
                # Trial swap
                best[i], best[j] = best[j], best[i]
                trial_cost = total_cost(best, type_positions)
                if trial_cost < best_cost - 0.01:
                    best_cost = trial_cost
                    improved = True
                else:
                    best[i], best[j] = best[j], best[i]  # revert

        if not improved:
            print(f"  Converged after {rnd+1} rounds")
            break
        print(f"  Round {rnd+1}: cost = {best_cost:.0f}")

    return best


def alternating_optimization(ordered_words, ring_radius, type_names, iterations=8):
    """
    Coordinate descent: alternate between optimizing word positions
    (local swaps on ring) and type positions (weighted centroids).
    """
    n = len(ordered_words)
    current_order = list(ordered_words)

    def positions_from_order(order):
        pos = {}
        for i, (word, cat, color) in enumerate(order):
            angle = 2 * math.pi * i / n - math.pi / 2
            pos[word] = (ring_radius * math.cos(angle),
                         ring_radius * math.sin(angle))
        return pos

    # Initial type positions
    wp = positions_from_order(current_order)
    type_pos = optimize_type_positions(wp, type_names)
    dist, cnt = compute_total_distance(wp, type_pos)
    print(f"\nAlternating optimization — iteration 0: distance={dist:.0f} ({cnt} edges)")

    for it in range(1, iterations + 1):
        print(f"\n--- Iteration {it} ---")
        # Step A: Fix type positions, refine word order via local swaps
        current_order = local_swap_refinement(current_order, ring_radius, type_pos)

        # Step B: Fix word order, re-optimize type positions
        wp = positions_from_order(current_order)
        type_pos = optimize_type_positions(wp, type_names)

        dist, cnt = compute_total_distance(wp, type_pos)
        print(f"  After iteration {it}: distance={dist:.0f}")

    return current_order, type_pos


def main():
    output_dir = sys.argv[1] if len(sys.argv) > 1 else "."

    # Flatten words
    all_words = []
    word_cat = {}
    word_color = {}
    for cat_name, cat_color, words in CATEGORIES:
        for w in words:
            all_words.append((w, cat_name, cat_color))
            word_cat[w] = cat_name
            word_color[w] = cat_color

    n = len(all_words)
    print(f"{n} words across {len(CATEGORIES)} categories")

    # ── Step 1: Spectral ordering ──
    order = spectral_order(all_words)
    ordered_words = [all_words[i] for i in order]
    print(f"Spectral ordering complete")

    # ── Step 2: Place words on ring, then alternating optimization ──
    RING_RADIUS = 28.0
    type_names = list(TYPES.keys())

    refined_order, type_positions = alternating_optimization(
        ordered_words, RING_RADIUS, type_names, iterations=8)
    ordered_words = refined_order

    # Rebuild final positions
    word_positions = {}
    word_angles = {}
    for i, (word, cat, color) in enumerate(ordered_words):
        angle = 2 * math.pi * i / n - math.pi / 2
        wx = RING_RADIUS * math.cos(angle)
        wy = RING_RADIUS * math.sin(angle)
        word_positions[word] = (wx, wy)
        word_angles[word] = angle

    total_dist, edge_count = compute_total_distance(word_positions, type_positions)
    print(f"\nFinal: {total_dist:.0f} total distance across {edge_count} edges")
    print(f"Mean edge length: {total_dist/max(edge_count,1):.1f}")

    # ── Step 4: Find category arc labels ──
    cat_arcs = {}
    for word, cat, _ in ordered_words:
        cat_arcs.setdefault(cat, []).append(word_angles[word])

    # ── Step 5: Generate dot file ──
    lines = []
    lines.append('digraph ETIL_TypeRing {')
    lines.append('  graph [size="60,60!" ratio=fill dpi=100 bgcolor="#1a1a2e"')
    lines.append('         pad="2.0" overlap=false splines=true')
    lines.append('         label=<<FONT FACE="Helvetica Neue" POINT-SIZE="42" COLOR="#e0e0e0">')
    lines.append(f'         ETIL v0.9.9 — Word / Type Map ({n} words, {len(type_names)} types, spectral layout)</FONT>>')
    lines.append('         labelloc=t labeljust=c];')
    lines.append('  node [fontname="Helvetica Neue" fontcolor="#e0e0e0"];')
    lines.append('  edge [penwidth=0.7];')
    lines.append('')

    # Type hexagons
    for tname, tcolor in TYPES.items():
        tx, ty = type_positions[tname]
        tid = f"T_{tname}"
        lines.append(f'  {tid} [')
        lines.append(f'    label=<<FONT POINT-SIZE="18"><B>{tname}</B></FONT>>')
        lines.append(f'    shape=hexagon style=filled fillcolor="{tcolor}" fontcolor="#1a1a2e"')
        lines.append(f'    width=2.2 height=1.8 fixedsize=true penwidth=2')
        lines.append(f'    pos="{tx:.4f},{ty:.4f}!"')
        lines.append(f'  ];')
    lines.append('')

    # Category labels at midpoint of each arc, inside the ring
    LABEL_RADIUS = 23.5
    cat_color_map = {c: col for c, col, _ in CATEGORIES}
    for cat_name, angles in cat_arcs.items():
        mid_angle = np.mean(angles)
        lx = LABEL_RADIUS * math.cos(mid_angle)
        ly = LABEL_RADIUS * math.sin(mid_angle)
        ccolor = cat_color_map.get(cat_name, "#888888")
        cid = f"C_{cat_name.replace('-','_').replace('/','_')}"
        lines.append(f'  {cid} [')
        lines.append(f'    label=<<FONT POINT-SIZE="14" COLOR="{ccolor}"><B>{escape_label(cat_name)}</B></FONT>>')
        lines.append(f'    shape=none width=0 height=0')
        lines.append(f'    pos="{lx:.4f},{ly:.4f}!"')
        lines.append(f'  ];')
    lines.append('')

    # Word rectangles
    word_ids = {}
    for i, (word, cat, color) in enumerate(ordered_words):
        wx, wy = word_positions[word]
        wid = sanitize(word) + f"_{i}"
        word_ids[word] = wid
        lines.append(f'  {wid} [')
        lines.append(f'    label=<<FONT POINT-SIZE="9">{escape_label(word)}</FONT>>')
        lines.append(f'    shape=box style="filled,rounded" fillcolor="#222244"')
        lines.append(f'    color="{color}" penwidth=1.5 margin="0.06,0.03"')
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

    dot_path = os.path.join(output_dir, 'etil-type-ring-spectral.dot')
    with open(dot_path, 'w') as f:
        f.write('\n'.join(lines))
    print(f"Wrote {dot_path}")


if __name__ == '__main__':
    main()
