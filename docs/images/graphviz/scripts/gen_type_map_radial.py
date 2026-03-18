#!/usr/bin/env python3
"""Generate ETIL word-type radial diagram for GraphViz neato."""
import math
import os
import sys

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
CATEGORIES = {
    "arithmetic": ["+","-","*","/","mod","/mod","negate","abs","max","min","1+","1-"],
    "stack": ["dup","drop","swap","over","rot","pick","nip","tuck","depth","?dup","roll","-rot"],
    "comparison": ["=","<>","<",">","<=",">=","0=","0<","0>","within"],
    "logic": ["true","false","not","and","or","xor","invert","lshift","rshift","lroll","rroll"],
    "i/o": [".","cr","emit","space","spaces","words","hex.","bin.","oct.",".|"],
    "math": ["sqrt","sin","cos","tan","tanh","asin","acos","atan","atan2","log","log2","log10",
             "exp","pow","ceil","floor","round","trunc","fmin","fmax","pi","f~",
             "random","random-seed","random-range"],
    "string": ["type","s+","s=","s<>","slength","substr","strim","sfind","sreplace",
               "ssplit","sjoin","sregex-find","sregex-replace","sregex-search","sregex-match",
               "sprintf","staint","s>upper","s>lower","s|"],
    "array": ["array-new","array-push","array-pop","array-get","array-set","array-length",
              "array-shift","array-unshift","array-compact","array-reverse","array-sort",
              "array-each","array-map","array-filter","array-reduce"],
    "byte-array": ["bytes-new","bytes-get","bytes-set","bytes-length","bytes-resize",
                   "bytes->string","string->bytes"],
    "map": ["map-new","map-set","map-get","map-remove","map-length","map-keys",
            "map-values","map-has?"],
    "json": ["json-parse","json-dump","json-pretty","json-get","json-length","json-type",
             "json-keys","json->map","json->array","map->json","array->json","json->value","j|"],
    "matrix": ["mat-new","mat-eye","array->mat","mat-diag","mat-rand","mat-get","mat-set",
               "mat-rows","mat-cols","mat-row","mat-col","mat*","mat+","mat-","mat-scale",
               "mat-transpose","mat-solve","mat-inv","mat-det","mat-eigen","mat-svd",
               "mat-lstsq","mat-norm","mat-trace","mat.","mat-relu","mat-sigmoid","mat-tanh",
               "mat-relu'","mat-sigmoid'","mat-tanh'","mat-hadamard","mat-add-col","mat-clip",
               "mat-randn","mat-sum","mat-col-sum","mat-mean","mat-softmax",
               "mat-cross-entropy","mat-apply","mat->array","mat->json","json->mat"],
    "observable": ["obs-from","obs-of","obs-empty","obs-range","obs-map","obs-map-with",
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
                   "obs-finalize","obs-switch-map","obs-catch"],
    "conversion": ["bool","int->float","float->int","number->string","string->number"],
    "execution": ["'","execute","xt?",">name","xt-body"],
    "memory": ["create",",","@","!","allot","variable","constant"],
    "time": ["time-us","us->iso","us->iso-us","time-iso","time-iso-us",
             "us->jd","jd->us","us->mjd","mjd->us","time-jd","time-mjd","sleep","elapsed"],
    "system": ["sys-semver","sys-timestamp","sys-datafields","sys-notification",
               "user-notification","abort"],
    "lvfs": ["cwd","cd","ls","ll","lr","cat"],
    "http": ["http-get","http-post"],
    "mongodb": ["mongo-find","mongo-count","mongo-insert","mongo-update","mongo-delete"],
    "control": ["if","else","then","do","loop","+loop","i","j","begin","until",
                "while","repeat","again",">r","r>","r@","leave","exit","recurse"],
    "dictionary": ["dict-forget","dict-forget-all","file-load","include","library",
                   "evaluate","marker","marker-restore","forget","forget-all"],
    "debug": ["s.","dump","see",".s"],
}

# ── Type connections: word → (input_types, output_types) ──
# Only characteristic types — not exhaustive
TYPE_LINKS = {
    "+": (["Integer","Float"], ["Integer","Float"]),
    "-": (["Integer","Float"], ["Integer","Float"]),
    "*": (["Integer","Float"], ["Integer","Float"]),
    "/": (["Integer","Float"], ["Integer","Float"]),
    "mod": (["Integer"], ["Integer"]),
    "negate": (["Integer","Float"], ["Integer","Float"]),
    "abs": (["Integer","Float"], ["Integer","Float"]),
    "=": (["Integer","Float","String"], ["Boolean"]),
    "<": (["Integer","Float"], ["Boolean"]),
    ">": (["Integer","Float"], ["Boolean"]),
    "0=": (["Integer"], ["Boolean"]),
    "within": (["Integer"], ["Boolean"]),
    "true": ([], ["Boolean"]),
    "false": ([], ["Boolean"]),
    "not": (["Boolean"], ["Boolean"]),
    "and": (["Boolean","Integer"], ["Boolean","Integer"]),
    "or": (["Boolean","Integer"], ["Boolean","Integer"]),
    "lshift": (["Integer"], ["Integer"]),
    "rshift": (["Integer"], ["Integer"]),
    ".": (["Integer","Float","String","Boolean"], []),
    "emit": (["Integer"], []),
    "hex.": (["Integer"], []),
    "sqrt": (["Float"], ["Float"]),
    "sin": (["Float"], ["Float"]),
    "pi": ([], ["Float"]),
    "random": ([], ["Float"]),
    "random-range": (["Integer"], ["Integer"]),
    "s+": (["String"], ["String"]),
    "slength": (["String"], ["Integer"]),
    "substr": (["String","Integer"], ["String"]),
    "ssplit": (["String"], ["Array"]),
    "sjoin": (["Array","String"], ["String"]),
    "staint": (["String"], ["Boolean"]),
    "sprintf": (["String"], ["String"]),
    "s>upper": (["String"], ["String"]),
    "array-new": ([], ["Array"]),
    "array-push": (["Array"], ["Array"]),
    "array-pop": (["Array"], ["Array"]),
    "array-get": (["Array","Integer"], []),
    "array-length": (["Array"], ["Integer"]),
    "array-each": (["Array","Xt"], []),
    "array-map": (["Array","Xt"], ["Array"]),
    "array-filter": (["Array","Xt"], ["Array"]),
    "array-reduce": (["Array","Xt"], []),
    "bytes-new": (["Integer"], ["ByteArray"]),
    "bytes-get": (["ByteArray","Integer"], ["Integer"]),
    "bytes->string": (["ByteArray"], ["String"]),
    "string->bytes": (["String"], ["ByteArray"]),
    "map-new": ([], ["Map"]),
    "map-set": (["Map","String"], ["Map"]),
    "map-get": (["Map","String"], []),
    "map-keys": (["Map"], ["Array"]),
    "map-values": (["Map"], ["Array"]),
    "map-has?": (["Map","String"], ["Boolean"]),
    "json-parse": (["String"], ["Json"]),
    "json-dump": (["Json"], ["String"]),
    "json-get": (["Json","String","Integer"], []),
    "json->map": (["Json"], ["Map"]),
    "json->array": (["Json"], ["Array"]),
    "map->json": (["Map"], ["Json"]),
    "array->json": (["Array"], ["Json"]),
    "json->value": (["Json"], []),
    "json-keys": (["Json"], ["Array"]),
    "json-length": (["Json"], ["Integer"]),
    "json-type": (["Json"], ["String"]),
    "mat-new": (["Integer"], ["Matrix"]),
    "mat-eye": (["Integer"], ["Matrix"]),
    "array->mat": (["Array"], ["Matrix"]),
    "mat-get": (["Matrix","Integer"], ["Float"]),
    "mat-set": (["Matrix","Integer","Float"], ["Matrix"]),
    "mat-rows": (["Matrix"], ["Integer"]),
    "mat*": (["Matrix"], ["Matrix"]),
    "mat+": (["Matrix"], ["Matrix"]),
    "mat-transpose": (["Matrix"], ["Matrix"]),
    "mat-solve": (["Matrix"], ["Matrix","Boolean"]),
    "mat-inv": (["Matrix"], ["Matrix","Boolean"]),
    "mat-det": (["Matrix"], ["Float","Boolean"]),
    "mat-relu": (["Matrix"], ["Matrix"]),
    "mat-softmax": (["Matrix"], ["Matrix"]),
    "mat-apply": (["Matrix","Xt"], ["Matrix"]),
    "mat->array": (["Matrix"], ["Array"]),
    "mat->json": (["Matrix"], ["Json"]),
    "json->mat": (["Json"], ["Matrix"]),
    "mat.": (["Matrix"], []),
    "mat-norm": (["Matrix"], ["Float"]),
    "mat-trace": (["Matrix"], ["Float"]),
    "mat-sum": (["Matrix"], ["Float"]),
    "obs-from": (["Array"], ["Observable"]),
    "obs-of": ([], ["Observable"]),
    "obs-range": (["Integer"], ["Observable"]),
    "obs-map": (["Observable","Xt"], ["Observable"]),
    "obs-filter": (["Observable","Xt"], ["Observable"]),
    "obs-reduce": (["Observable","Xt"], []),
    "obs-take": (["Observable","Integer"], ["Observable"]),
    "obs-subscribe": (["Observable","Xt"], []),
    "obs-to-array": (["Observable"], ["Array"]),
    "obs-count": (["Observable"], ["Integer"]),
    "obs?": ([], ["Boolean"]),
    "obs-kind": (["Observable"], ["String"]),
    "obs-merge": (["Observable"], ["Observable"]),
    "obs-timer": (["Integer"], ["Observable"]),
    "obs-read-bytes": (["String"], ["Observable"]),
    "obs-read-lines": (["String"], ["Observable"]),
    "obs-http-get": (["String","Map"], ["Observable"]),
    "int->float": (["Integer"], ["Float"]),
    "float->int": (["Float"], ["Integer"]),
    "number->string": (["Integer","Float"], ["String"]),
    "string->number": (["String"], ["Integer","Float"]),
    "bool": ([], ["Boolean"]),
    "'": ([], ["Xt"]),
    "execute": (["Xt"], []),
    "xt?": ([], ["Boolean"]),
    ">name": (["Xt"], ["String"]),
    "time-us": ([], ["Integer"]),
    "us->iso": (["Integer"], ["String"]),
    "elapsed": (["Xt"], ["Integer"]),
    "http-get": (["String","Map"], ["ByteArray","Integer","Boolean"]),
    "http-post": (["String","Map","ByteArray"], ["ByteArray","Integer","Boolean"]),
    "mongo-find": (["String","Json","Map"], ["Json","Boolean"]),
    "mongo-count": (["String","Json","Map"], ["Integer","Boolean"]),
    "mongo-insert": (["String","Json","Map"], ["Boolean"]),
    "@": ([], []),
    "!": ([], []),
    "if": (["Boolean"], []),
    "until": (["Boolean"], []),
    "while": (["Boolean"], []),
    "i": ([], ["Integer"]),
    "j": ([], ["Integer"]),
    "cat": (["String"], []),
    "cd": (["String"], []),
    "evaluate": (["String"], []),
}

# ── Category colors ──
CAT_COLORS = {
    "arithmetic": "#00d4ff", "stack": "#636e72", "comparison": "#55efc4",
    "logic": "#ffeaa7", "i/o": "#dfe6e9", "math": "#feca57",
    "string": "#fd79a8", "array": "#a29bfe", "byte-array": "#ff7675",
    "map": "#74b9ff", "json": "#fab1a0", "matrix": "#00cec9",
    "observable": "#e17055", "conversion": "#81ecec", "execution": "#b2bec3",
    "memory": "#c8d6e5", "time": "#ffeaa7", "system": "#636e72",
    "lvfs": "#a29bfe", "http": "#ff7675", "mongodb": "#fab1a0",
    "control": "#dfe6e9", "dictionary": "#636e72", "debug": "#b2bec3",
}

# ── Layout parameters ──
CENTER_X, CENTER_Y = 0, 0
TYPE_RING_RADIUS = 5.0    # hexagons
CAT_RING_RADIUS = 20.0    # category centers
WORD_RADIUS_BASE = 2.5    # radius of word ring around category center
WORD_RADIUS_PER = 0.055   # additional radius per word
IMAGE_SIZE = 54            # inches (square)

def sanitize(name):
    """Make a valid graphviz node ID."""
    return "w_" + name.replace("+","_plus").replace("-","_").replace("*","_star") \
                      .replace("/","_slash").replace(".","_dot").replace("?","_q") \
                      .replace("!","_bang").replace("@","_at").replace(",","_comma") \
                      .replace("'","_tick").replace(">","_gt").replace("<","_lt") \
                      .replace("=","_eq").replace("|","_pipe").replace("~","_tilde")

def escape_label(name):
    """Escape for graphviz HTML labels."""
    return name.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;").replace("'","&apos;")

def main():
    output_dir = sys.argv[1] if len(sys.argv) > 1 else "."

    lines = []
    lines.append('digraph ETIL_TypeMap {')
    lines.append(f'  graph [size="{IMAGE_SIZE},{IMAGE_SIZE}!" ratio=fill dpi=120')
    lines.append('         bgcolor="#1a1a2e" pad="1.0" overlap=false splines=true];')
    lines.append('  node [fontname="Helvetica Neue" fontcolor="#e0e0e0"];')
    lines.append('  edge [penwidth=0.6];')
    lines.append('')

    # ── Type hexagons in center ──
    type_list = list(TYPES.items())
    n_types = len(type_list)
    for i, (tname, tcolor) in enumerate(type_list):
        angle = 2 * math.pi * i / n_types - math.pi / 2
        tx = CENTER_X + TYPE_RING_RADIUS * math.cos(angle)
        ty = CENTER_Y + TYPE_RING_RADIUS * math.sin(angle)
        tid = f"type_{tname}"
        lines.append(f'  {tid} [')
        lines.append(f'    label=<<FONT POINT-SIZE="16"><B>{tname}</B></FONT>>')
        lines.append(f'    shape=hexagon style=filled fillcolor="{tcolor}" fontcolor="#1a1a2e"')
        lines.append(f'    width=1.8 height=1.5 fixedsize=true')
        lines.append(f'    pos="{tx:.3f},{ty:.3f}!"')
        lines.append(f'  ];')

    lines.append('')

    # ── Category radials around periphery ──
    cat_list = list(CATEGORIES.items())
    n_cats = len(cat_list)

    all_word_nodes = {}  # word_sanitized → (x, y, cat_name)

    for ci, (cat_name, words) in enumerate(cat_list):
        cat_angle = 2 * math.pi * ci / n_cats - math.pi / 2
        cx = CENTER_X + CAT_RING_RADIUS * math.cos(cat_angle)
        cy = CENTER_Y + CAT_RING_RADIUS * math.sin(cat_angle)
        cat_color = CAT_COLORS.get(cat_name, "#888888")
        cat_id = f"cat_{cat_name.replace('-','_').replace('/','_')}"

        # Category center node
        lines.append(f'  {cat_id} [')
        lines.append(f'    label=<<FONT POINT-SIZE="15"><B>{escape_label(cat_name)}</B></FONT>>')
        lines.append(f'    shape=circle style=filled fillcolor="#2d3436" color="{cat_color}"')
        lines.append(f'    penwidth=3 width=1.8 fixedsize=true')
        lines.append(f'    pos="{cx:.3f},{cy:.3f}!"')
        lines.append(f'  ];')

        # Word leaf nodes in radial ring
        n_words = len(words)
        word_radius = WORD_RADIUS_BASE + WORD_RADIUS_PER * n_words
        for wi, word in enumerate(words):
            word_angle = 2 * math.pi * wi / n_words - math.pi / 2
            wx = cx + word_radius * math.cos(word_angle)
            wy = cy + word_radius * math.sin(word_angle)
            wid = sanitize(word) + f"_{cat_name.replace('-','_').replace('/','_')}"
            all_word_nodes[wid] = (wx, wy, cat_name, word)

            lines.append(f'  {wid} [')
            lines.append(f'    label=<<FONT POINT-SIZE="10">{escape_label(word)}</FONT>>')
            lines.append(f'    shape=box style="filled,rounded" fillcolor="#222244"')
            lines.append(f'    color="{cat_color}" penwidth=1 width=0.01 height=0.01')
            lines.append(f'    margin="0.04,0.02"')
            lines.append(f'    pos="{wx:.3f},{wy:.3f}!"')
            lines.append(f'  ];')

            # Invisible edge to category center (structural)
            lines.append(f'  {cat_id} -> {wid} [style=invis weight=10];')

    lines.append('')

    # ── Type connection edges ──
    lines.append('  // Type connections')
    for wid, (wx, wy, cat_name, word) in all_word_nodes.items():
        if word not in TYPE_LINKS:
            continue
        inputs, outputs = TYPE_LINKS[word]
        for tname in inputs:
            tcolor = TYPES.get(tname, "#888888")
            tid = f"type_{tname}"
            lines.append(f'  {tid} -> {wid} [color="{tcolor}40" arrowsize=0.4];')
        for tname in outputs:
            tcolor = TYPES.get(tname, "#888888")
            tid = f"type_{tname}"
            lines.append(f'  {wid} -> {tid} [color="{tcolor}40" arrowsize=0.4];')

    lines.append('}')

    dot_path = os.path.join(output_dir, 'etil-type-map-radial.dot')
    with open(dot_path, 'w') as f:
        f.write('\n'.join(lines))
    print(f"Generated {len(all_word_nodes)} word nodes across {n_cats} categories")
    print(f"Wrote {dot_path}")

if __name__ == '__main__':
    main()
