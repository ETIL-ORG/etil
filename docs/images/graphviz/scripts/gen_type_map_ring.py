#!/usr/bin/env python3
"""Generate ETIL word-type ring diagram — words on periphery, types in center."""
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

# ── Categories and words (sequential order around ring) ──
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

def main():
    output_dir = sys.argv[1] if len(sys.argv) > 1 else "."

    # Flatten all words with their category info
    all_words = []
    for cat_name, cat_color, words in CATEGORIES:
        for w in words:
            all_words.append((w, cat_name, cat_color))

    total = len(all_words)
    RING_RADIUS = 28.0
    TYPE_RADIUS = 5.0

    lines = []
    lines.append('digraph ETIL_TypeRing {')
    lines.append('  graph [size="60,60!" ratio=fill dpi=100 bgcolor="#1a1a2e"')
    lines.append('         pad="2.0" overlap=false splines=true')
    lines.append('         label=<<FONT FACE="Helvetica Neue" POINT-SIZE="42" COLOR="#e0e0e0">')
    lines.append(f'         ETIL v0.9.9 — Word/Type Map ({total} words, {len(TYPES)} types)</FONT>>')
    lines.append('         labelloc=t labeljust=c];')
    lines.append('  node [fontname="Helvetica Neue" fontcolor="#e0e0e0"];')
    lines.append('  edge [penwidth=0.7];')
    lines.append('')

    # ── Type hexagons in center ──
    type_list = list(TYPES.items())
    n_types = len(type_list)
    for i, (tname, tcolor) in enumerate(type_list):
        angle = 2 * math.pi * i / n_types - math.pi / 2
        tx = TYPE_RADIUS * math.cos(angle)
        ty = TYPE_RADIUS * math.sin(angle)
        tid = f"T_{tname}"
        lines.append(f'  {tid} [')
        lines.append(f'    label=<<FONT POINT-SIZE="18"><B>{tname}</B></FONT>>')
        lines.append(f'    shape=hexagon style=filled fillcolor="{tcolor}" fontcolor="#1a1a2e"')
        lines.append(f'    width=2.2 height=1.8 fixedsize=true penwidth=2')
        lines.append(f'    pos="{tx:.4f},{ty:.4f}!"')
        lines.append(f'  ];')
    lines.append('')

    # ── Category label nodes ──
    cat_spans = []
    idx = 0
    for cat_name, cat_color, words in CATEGORIES:
        start_angle = 2 * math.pi * idx / total - math.pi / 2
        end_angle = 2 * math.pi * (idx + len(words) - 1) / total - math.pi / 2
        mid_angle = (start_angle + end_angle) / 2
        cat_spans.append((cat_name, cat_color, mid_angle, len(words)))
        idx += len(words)

    LABEL_RADIUS = 22.0
    for cat_name, cat_color, mid_angle, count in cat_spans:
        lx = LABEL_RADIUS * math.cos(mid_angle)
        ly = LABEL_RADIUS * math.sin(mid_angle)
        cid = f"C_{cat_name.replace('-','_').replace('/','_')}"
        lines.append(f'  {cid} [')
        lines.append(f'    label=<<FONT POINT-SIZE="16" COLOR="{cat_color}"><B>{escape_label(cat_name)}</B></FONT>>')
        lines.append(f'    shape=none width=0 height=0')
        lines.append(f'    pos="{lx:.4f},{ly:.4f}!"')
        lines.append(f'  ];')
    lines.append('')

    # ── Word rectangles around periphery ──
    word_ids = {}
    for i, (word, cat_name, cat_color) in enumerate(all_words):
        angle = 2 * math.pi * i / total - math.pi / 2
        wx = RING_RADIUS * math.cos(angle)
        wy = RING_RADIUS * math.sin(angle)
        wid = sanitize(word) + f"_{cat_name.replace('-','_').replace('/','_')}"
        word_ids[word] = wid

        lines.append(f'  {wid} [')
        lines.append(f'    label=<<FONT POINT-SIZE="9">{escape_label(word)}</FONT>>')
        lines.append(f'    shape=box style="filled,rounded" fillcolor="#222244"')
        lines.append(f'    color="{cat_color}" penwidth=1.5 margin="0.06,0.03"')
        lines.append(f'    pos="{wx:.4f},{wy:.4f}!"')
        lines.append(f'  ];')
    lines.append('')

    # ── Type connection edges ──
    lines.append('  // Input edges: Type -> Word')
    for word, wid in word_ids.items():
        if word not in TYPE_LINKS:
            continue
        inputs, outputs = TYPE_LINKS[word]
        for tname in inputs:
            tcolor = TYPES.get(tname, "#888888")
            tid = f"T_{tname}"
            lines.append(f'  {tid} -> {wid} [color="{tcolor}50" arrowsize=0.35 arrowhead=normal];')

    lines.append('')
    lines.append('  // Output edges: Word -> Type')
    for word, wid in word_ids.items():
        if word not in TYPE_LINKS:
            continue
        inputs, outputs = TYPE_LINKS[word]
        for tname in outputs:
            tcolor = TYPES.get(tname, "#888888")
            tid = f"T_{tname}"
            lines.append(f'  {wid} -> {tid} [color="{tcolor}50" arrowsize=0.35 arrowhead=normal];')

    lines.append('}')

    dot_path = os.path.join(output_dir, 'etil-type-ring.dot')
    with open(dot_path, 'w') as f:
        f.write('\n'.join(lines))
    print(f"Generated {total} words, {n_types} types, {len(cat_spans)} categories")
    print(f"Wrote {dot_path}")

if __name__ == '__main__':
    main()
