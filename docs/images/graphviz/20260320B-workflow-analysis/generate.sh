#!/usr/bin/env bash
# Generate all slide .dot files and render to PNG + SVG
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# ============================================================
# Common graph preamble for slides
# ============================================================
slide_preamble() {
    cat <<'PREAMBLE'
    size="7.8,4.5!";
    ratio=fill;
    rankdir=LR;
    fontname="Helvetica Neue";
    bgcolor="white";
    newrank=true;
    nodesep=0.4;
    ranksep=0.8;
    pad="0.3,0.2";

    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];
PREAMBLE
}

# Wing node (prev or next) - small, muted
wing_node() {
    local id="$1" label="$2" color="$3"
    cat <<EOF
    ${id} [
        label=<<font point-size="8"><b>${label}</b></font>>
        fillcolor="${color}"
        fontcolor="#888888"
        fontsize=8
        width=1.3
        height=0.6
        penwidth=1
        style="filled,rounded,dashed"
    ];
EOF
}

# ============================================================
# Colors
# ============================================================
C_DESIGN="#4A90D9"       # blue
C_PLAN="#7BC67E"         # green
C_REVIEW="#F5A623"       # orange
C_GAP="#D0021B"          # red
C_STATUS="#BD10E0"       # purple
C_OPS="#9B9B9B"          # gray

# Faded wing versions
W_DESIGN="#B8D4EE"
W_PLAN="#C4E2C6"
W_REVIEW="#FCDBA3"
W_GAP="#EBA0A8"
W_STATUS="#DDA8F0"
W_OPS="#CCCCCC"
W_NONE="#E8E8E8"

# Back-reference polygon
C_BACKREF="#FFF3E0"
C_BACKREF_BORDER="#E65100"

# ============================================================
# SLIDE 01: MLP Design Analysis
# ============================================================
cat > slide-01.dot <<'DOT'
digraph slide01 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    prev [label=<<font point-size="9" color="#AAAAAA"><b>START</b><br/>v0.8.15</font>>
        fillcolor="#E8E8E8" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed" shape=ellipse];

    curr [
        label=<<b><font point-size="12">MLP Design Analysis</font></b><br/><font point-size="9" color="#DDDDDD">March 10 · v0.8.15 · DESIGN</font><br/><br/><font point-size="8" color="white">Analyzed ETIL matrix subsystem for MLP feasibility.</font><br/><font point-size="8" color="white">Found <b>8 critical gaps</b> across 4 categories:</font><br/><br/><font point-size="8" color="#DDDDDD">• Element-wise ops (mat-hadamard, mat-add-col)</font><br/><font point-size="8" color="#DDDDDD">• Activation functions (relu, sigmoid, tanh + derivatives)</font><br/><font point-size="8" color="#DDDDDD">• Weight initialization (mat-randn, xavier, he)</font><br/><font point-size="8" color="#DDDDDD">• Loss functions (MSE, softmax, cross-entropy)</font><br/><br/><font point-size="8" color="white">Recommended deferring in-place ops.</font>>
        fillcolor="#4A90D9" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#2A70B9"
    ];

    next [label=<<font point-size="8"><b>MLP Primitives Plan</b><br/>Mar 10</font>>
        fillcolor="#C4E2C6" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999"> </font>>];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">informs</font>>];
}
DOT

# ============================================================
# SLIDE 02: MLP Primitives Plan
# ============================================================
cat > slide-02.dot <<'DOT'
digraph slide02 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    prev [label=<<font point-size="8"><b>MLP Design Analysis</b><br/>Mar 10</font>>
        fillcolor="#B8D4EE" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="12">MLP Primitives Plan</font></b><br/><font point-size="9" color="#333333">March 10 · v0.8.18→v0.8.21 · PLAN</font><br/><br/><font point-size="8">Broke 17 new C++ primitives into 3 stages:</font><br/><br/><font point-size="8"><b>Stage 1 — Forward Pass</b> (6 primitives)<br/>mat-relu, mat-sigmoid, mat-tanh,<br/>mat-hadamard, mat-add-col, mat-randn</font><br/><br/><font point-size="8"><b>Stage 2 — Backpropagation</b> (7 primitives)<br/>mat-relu', mat-sigmoid', mat-tanh',<br/>mat-sum, mat-col-sum, mat-mean, mat-clip</font><br/><br/><font point-size="8"><b>Stage 3 — Classification</b> (4 primitives)<br/>mat-softmax, mat-cross-entropy, mat-apply, tanh</font>>
        fillcolor="#7BC67E" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#5BA65E"
    ];

    next [label=<<font point-size="8"><b>Observable Design</b><br/>Mar 12</font>>
        fillcolor="#B8D4EE" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">new track</font>>];
}
DOT

# ============================================================
# SLIDE 03: Observable Design
# ============================================================
cat > slide-03.dot <<'DOT'
digraph slide03 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    prev [label=<<font point-size="8"><b>MLP Primitives Plan</b><br/>Mar 10</font>>
        fillcolor="#C4E2C6" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="12">Observable Design</font></b><br/><font point-size="9" color="#DDDDDD">March 12 · DESIGN</font><br/><br/><font point-size="8" color="white">Designed HeapObservable: linked-list pipeline<br/>nodes with per-node state for closure emulation.</font><br/><br/><font point-size="8" color="#DDDDDD">Chose <b>Approach 3B</b> over alternatives:</font><br/><font point-size="8" color="#DDDDDD">• 3A: CREATE/DOES&gt; (too FORTH-specific)</font><br/><font point-size="8" color="#DDDDDD">• 3C: stack passing (too fragile)</font><br/><br/><font point-size="8" color="white">21 core operators: creation, transform,<br/>accumulate, limiting, combination, terminal.</font>>
        fillcolor="#4A90D9" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#2A70B9"
    ];

    next [label=<<font point-size="8"><b>Temporal + AVO</b><br/>Mar 15-16</font>>
        fillcolor="#B8D4EE" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">parallel track</font>>];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">extends</font>>];
}
DOT

# ============================================================
# SLIDE 04: Temporal + AVO Design+Plan
# ============================================================
cat > slide-04.dot <<'DOT'
digraph slide04 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    backref [
        label=<<font point-size="8"><b>← Observable Design</b><br/>Mar 12 · extends + evolves</font>>
        shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C"
        fontsize=8 penwidth=1.5 width=2.0
    ];

    prev [label=<<font point-size="8"><b>Observable Design</b><br/>Mar 12</font>>
        fillcolor="#B8D4EE" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="12">Temporal + AVO Extensions</font></b><br/><font point-size="9" color="#333333">March 15-16 · DESIGN + PLAN (4 documents)</font><br/><br/><font point-size="8"><b>Temporal Design+Plan</b> (Mar 15)<br/>13 temporal operators in 3 tiers:<br/>debounce, throttle, delay, timeout,<br/>buffer-time, audit-time, retry-delay...</font><br/><br/><font point-size="8"><b>AVO Design+Plan</b> (Mar 16)<br/>Vision: unify ALL I/O as Observable streams.<br/>4 phases: buffer → file → HTTP → remove sync</font>>
        fillcolor="#4A90D9" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#2A70B9"
    ];

    next [label=<<font point-size="8"><b>Build Ops</b><br/>Mar 16</font>>
        fillcolor="#CCCCCC" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    backref -> curr [style=dashed color="#E65100" penwidth=1.5 arrowhead=vee];
    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
}
DOT

# ============================================================
# SLIDE 05: Build Ops + Long Jobs
# ============================================================
cat > slide-05.dot <<'DOT'
digraph slide05 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    prev [label=<<font point-size="8"><b>Temporal + AVO</b><br/>Mar 15-16</font>>
        fillcolor="#B8D4EE" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="12">Process Improvements</font></b><br/><font point-size="9" color="#333333">March 16 · OPS (2 documents)</font><br/><br/><font point-size="8"><b>Build Ops Design</b><br/>Problem: clean builds took 4-6 minutes.<br/>Solution: pre-built deps → ~25s clean builds,<br/>debug-first workflow, super-push.sh.<br/>Impact: 10x faster iteration cycle.</font><br/><br/><font point-size="8"><b>Long-Running Jobs Policy</b><br/>Problem: CI builds blocked conversation context.<br/>Solution: background agents for CI monitoring,<br/>full output to /tmp/*.log, PASS/FAIL summary.</font>>
        fillcolor="#9B9B9B" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#777777"
    ];

    next [label=<<font point-size="8"><b>RxJS Gap Analysis</b><br/>Mar 17</font>>
        fillcolor="#EBA0A8" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
}
DOT

# ============================================================
# SLIDE 06: RxJS Gap Analysis + Gap Fill Plan
# ============================================================
cat > slide-06.dot <<'DOT'
digraph slide06 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    backref [
        label=<<font point-size="8"><b>← Observable Design</b><br/>Mar 12 · reviews what's built</font>>
        shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C"
        fontsize=8 penwidth=1.5 width=2.0
    ];

    prev [label=<<font point-size="8"><b>Build Ops</b><br/>Mar 16</font>>
        fillcolor="#CCCCCC" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="12">RxJS Gap → Gap Fill Plan</font></b><br/><font point-size="9" color="white">March 17 · GAP ANALYSIS + PLAN</font><br/><br/><font point-size="8" color="#FFCCCC"><b>RxJS Gap Analysis</b><br/>Scored 40+ RxJS operators on two axes:<br/>applicability (high/med/low) × difficulty (easy/med/hard).<br/>Identified 10 high-applicability, easy operators.</font><br/><br/><font point-size="8" color="#CCFFCC"><b>Observable Gap Fill Plan</b><br/>Two stages prioritized by gap analysis:<br/>Stage 1: 8 easy operators<br/>Stage 2: 2 medium operators (switch-map, catch)</font>>
        fillcolor="#C62828" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#8E0000"
    ];

    next [label=<<font point-size="8"><b>MLP Status Report</b><br/>Mar 18</font>>
        fillcolor="#DDA8F0" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    backref -> curr [style=dashed color="#E65100" penwidth=1.5 arrowhead=vee];
    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
}
DOT

# ============================================================
# SLIDE 07: MLP Status Report
# ============================================================
cat > slide-07.dot <<'DOT'
digraph slide07 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    backref [
        label=<<font point-size="8"><b>← MLP Design Analysis</b><br/>Mar 10 · references sections 2-6</font>>
        shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C"
        fontsize=8 penwidth=1.5 width=2.2
    ];

    prev [label=<<font point-size="8"><b>RxJS Gap + Plan</b><br/>Mar 17</font>>
        fillcolor="#EBA0A8" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="12">MLP Status Report</font></b><br/><font point-size="9" color="white">March 18 · v1.0.0 · STATUS</font><br/><br/><font point-size="8" color="white">Assessed design document sections 2-6:</font><br/><br/><font point-size="8" color="#90EE90">✓ All 17 C++ primitives implemented</font><br/><font point-size="8" color="#90EE90">✓ mat-col-vec added (18th, not in plan)</font><br/><font point-size="8" color="#FFB3B3">✗ TIL-level MLP library: NOT DONE</font><br/><font point-size="8" color="#FFB3B3">✗ Selection engine: CRITICAL GAP</font><br/><font point-size="8" color="#FFB3B3">✗ Evolution engine: NOT STARTED</font><br/><br/><font point-size="8" color="white"><b>Verdict:</b> Primitives work. Need library + engines.</font>>
        fillcolor="#9C27B0" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#7B1FA2"
    ];

    next [label=<<font point-size="8"><b>Layers 1-3 Plan</b><br/>Mar 18</font>>
        fillcolor="#C4E2C6" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    backref -> curr [style=dashed color="#E65100" penwidth=1.5 arrowhead=vee];
    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">gaps drive</font>>];
}
DOT

# ============================================================
# SLIDE 08: Layers 1-3 Plan + Review
# ============================================================
cat > slide-08.dot <<'DOT'
digraph slide08 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    backref [
        label=<<font point-size="8"><b>← MLP Design Analysis</b><br/>Mar 10 · implements sections 3-6</font>>
        shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C"
        fontsize=8 penwidth=1.5 width=2.2
    ];

    prev [label=<<font point-size="8"><b>MLP Status Report</b><br/>Mar 18</font>>
        fillcolor="#DDA8F0" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="12">Layers 1-3 Plan + Review</font></b><br/><font point-size="9" color="#333333">March 18 · v1.3.0 · PLAN + REVIEW (same day!)</font><br/><br/><font point-size="8"><b>Plan:</b> Three layers to close status report gaps:</font><br/><font point-size="8">L1: TIL MLP library (mlp.til) — 275 lines</font><br/><font point-size="8">L2: SelectionEngine — 4 strategies</font><br/><font point-size="8">L3: EvolutionEngine — fitness evaluation</font><br/><br/><font point-size="8"><b>Review:</b> All 3 layers implemented same day.</font><br/><font point-size="8" color="#2E7D32">XOR trains to &lt;0.001 MSE in 2000 epochs.</font><br/><br/><font point-size="8" color="#C62828"><b>But:</b> bytecode-level random mutation<br/>produces garbage → need structural approach.</font>>
        fillcolor="#F5A623" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#C68400"
    ];

    next [label=<<font point-size="8"><b>AST Evolution Design</b><br/>Mar 19</font>>
        fillcolor="#B8D4EE" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    backref -> curr [style=dashed color="#E65100" penwidth=1.5 arrowhead=vee];
    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">gaps drive</font>>];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">insight drives</font>>];
}
DOT

# ============================================================
# SLIDE 09: AST Evolution Design
# ============================================================
cat > slide-09.dot <<'DOT'
digraph slide09 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    prev [label=<<font point-size="8"><b>Layers 1-3 Review</b><br/>Mar 18</font>>
        fillcolor="#FCDBA3" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="12">AST Evolution Design</font></b><br/><font point-size="9" color="#DDDDDD">March 19 · 500+ lines · DESIGN</font><br/><br/><font point-size="8" color="white"><b>Key insight:</b> Don't mutate bytecode randomly.</font><br/><font point-size="8" color="white">Decompile to AST → mutate structurally → recompile.</font><br/><br/><font point-size="8" color="#DDDDDD"><b>7-stage pipeline:</b></font><br/><font point-size="8" color="#DDDDDD">0. Marker opcodes (compiler hints)</font><br/><font point-size="8" color="#DDDDDD">1. Bytecode → AST decompiler</font><br/><font point-size="8" color="#DDDDDD">2. AST → bytecode compiler</font><br/><font point-size="8" color="#DDDDDD">3. Stack simulator + type inference</font><br/><font point-size="8" color="#DDDDDD">4. Type-directed repair</font><br/><font point-size="8" color="#DDDDDD">5. Semantic tags for tiered substitution</font><br/><font point-size="8" color="#DDDDDD">6. AST genetic operators (4 types)</font>>
        fillcolor="#4A90D9" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#2A70B9"
    ];

    next [label=<<font point-size="8"><b>AST Evolution Plan</b><br/>Mar 19</font>>
        fillcolor="#C4E2C6" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">mutation fails →<br/>structural approach</font>>];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">informs</font>>];
}
DOT

# ============================================================
# SLIDE 10: AST Evolution Plan
# ============================================================
cat > slide-10.dot <<'DOT'
digraph slide10 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    prev [label=<<font point-size="8"><b>AST Evolution Design</b><br/>Mar 19</font>>
        fillcolor="#B8D4EE" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="12">AST Evolution Plan</font></b><br/><font point-size="9" color="#333333">March 19 · v1.3.2→v1.5.0 · PLAN</font><br/><br/><font point-size="8">Each of 7 stages independently testable:</font><br/><br/><font point-size="8">Stage 0: BlockBegin/BlockEnd marker opcodes</font><br/><font point-size="8">Stage 1: AST types + recursive descent decompiler</font><br/><font point-size="8">Stage 2: AST compiler with backpatched branches</font><br/><font point-size="8">Stage 3: Stack simulator, type inference</font><br/><font point-size="8">Stage 4: Type-directed repair (swap/rot/roll)</font><br/><font point-size="8">Stage 5: Semantic tags on 22 words</font><br/><font point-size="8">Stage 6: 4 genetic operators + engine integration</font><br/><br/><font point-size="8"><b>66 new tests</b> across all stages.</font>>
        fillcolor="#7BC67E" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#5BA65E"
    ];

    next [label=<<font point-size="8"><b>Stage 6 Gap Analysis</b><br/>Mar 20</font>>
        fillcolor="#EBA0A8" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">done → audit</font>>];
}
DOT

# ============================================================
# SLIDE 11: Stage 6 Gap + Completion + Warnings
# ============================================================
cat > slide-11.dot <<'DOT'
digraph slide11 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    backref [
        label=<<font point-size="8"><b>← AST Evolution Plan</b><br/>Mar 19 · audits Stage 6</font>>
        shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C"
        fontsize=8 penwidth=1.5 width=2.0
    ];

    prev [label=<<font point-size="8"><b>AST Evolution Plan</b><br/>Mar 19</font>>
        fillcolor="#C4E2C6" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="12">Gap Analysis + Completion + Cleanup</font></b><br/><font point-size="9" color="white">March 20 · GAP + PLAN + PLAN (3 documents)</font><br/><br/><font point-size="8" color="#FFCCCC"><b>Stage 6 Gap Analysis</b> — found bugs:<br/>• Roll type-stack not updated in type_repair.cpp<br/>• Fitness limits allow DoS (SIZE_MAX)<br/>• move_block + control_flow_mutation missing</font><br/><br/><font point-size="8" color="#CCFFCC"><b>Stage 6 Completion Plan</b> — 5 phases:<br/>P0 fixes → DRY → operators → tests → type inference</font><br/><br/><font point-size="8" color="#CCFFCC"><b>Build Warning Cleanup</b> — parallel effort:<br/>27 warnings eliminated, GCC LTO ICE → -flto off</font>>
        fillcolor="#C62828" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#8E0000"
    ];

    next [label=<<font point-size="8"><b>MLP Goal Assessment</b><br/>Mar 20</font>>
        fillcolor="#FCDBA3" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    backref -> curr [style=dashed color="#E65100" penwidth=1.5 arrowhead=vee];
    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">done → audit</font>>];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee label=<<font point-size="7" color="#999999">enables</font>>];
}
DOT

# ============================================================
# SLIDE 12: MLP Goal Assessment (FINALE)
# ============================================================
cat > slide-12.dot <<'DOT'
digraph slide12 {
    size="7.8,4.5!"; ratio=fill; rankdir=LR;
    fontname="Helvetica Neue"; bgcolor="white";
    newrank=true; nodesep=0.4; ranksep=0.8; pad="0.3,0.2";
    node [fontname="Helvetica Neue" style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue"];

    backref1 [
        label=<<font point-size="8"><b>← MLP Design Analysis</b><br/>Mar 10 · compares v0.8.15 goals</font>>
        shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C"
        fontsize=8 penwidth=1.5 width=2.2
    ];

    backref2 [
        label=<<font point-size="8"><b>← MLP Status Report</b><br/>Mar 18 · compares v1.0.0 status</font>>
        shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C"
        fontsize=8 penwidth=1.5 width=2.2
    ];

    prev [label=<<font point-size="8"><b>Gap + Completion</b><br/>Mar 20</font>>
        fillcolor="#EBA0A8" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed"];

    curr [
        label=<<b><font point-size="13">MLP Goal Assessment</font></b><br/><font point-size="9" color="#333333">March 20 · v1.6.0 · ASSESSMENT</font><br/><br/><font point-size="8"><b>7/8 design sections complete</b></font><br/><font point-size="8">1 deferred by design (in-place ops)</font><br/><br/><font point-size="8" color="#2E7D32">✓ 17 C++ primitives  ✓ TIL MLP library</font><br/><font point-size="8" color="#2E7D32">✓ 4-strategy SelectionEngine</font><br/><font point-size="8" color="#2E7D32">✓ AST-level evolution pipeline</font><br/><font point-size="8" color="#2E7D32">✓ Compile-time type inference</font><br/><font point-size="8" color="#2E7D32">✓ 1362 tests, zero warnings</font><br/><br/><font point-size="10"><i>"The Evolutionary in ETIL</i></font><br/><font point-size="10"><i>is no longer aspirational."</i></font>>
        fillcolor="#F5A623" fontcolor="white" fontsize=10 width=3.8 penwidth=3 color="#C68400"
    ];

    next [label=<<font point-size="9" color="#AAAAAA"><b>END</b><br/>v1.6.0</font>>
        fillcolor="#E8E8E8" fontcolor="#888888" fontsize=8 width=1.3 height=0.6
        penwidth=1 style="filled,rounded,dashed" shape=ellipse];

    backref1 -> curr [style=dashed color="#E65100" penwidth=1.5 arrowhead=vee];
    backref2 -> curr [style=dashed color="#E65100" penwidth=1.5 arrowhead=vee];
    prev -> curr [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
    curr -> next [style=dotted color="#BBBBBB" penwidth=1.5 arrowhead=vee];
}
DOT

# ============================================================
# OVERVIEW (no size constraint)
# ============================================================
cat > slide-00-overview.dot <<'DOT'
digraph overview {
    rankdir=LR;
    fontname="Helvetica Neue";
    fontsize=14;
    bgcolor="white";
    newrank=true;
    nodesep=0.5;
    ranksep=1.0;
    pad=0.6;

    node [fontname="Helvetica Neue" fontsize=9 style="filled,rounded" shape=box penwidth=1.5];
    edge [fontname="Helvetica Neue" fontsize=7];

    // Title
    title [
        label=<<font point-size="20"><b>ETIL Design-Plan-Review Workflow</b></font><br/><font point-size="12" color="#666666">20 documents · 4 iterations · 10 days · v0.8.15 → v1.6.0</font><br/><font point-size="10" color="#999999">Each iteration's review feeds the next iteration's design</font>>
        shape=plaintext style=""
    ];

    // Legend row
    subgraph cluster_legend {
        label=""; style=invis; margin=8;
        node [fontsize=8 width=0.9 height=0.3];
        l_d [label="Design" fillcolor="#4A90D9" fontcolor="white"];
        l_p [label="Plan" fillcolor="#7BC67E" fontcolor="white"];
        l_r [label="Review / Assessment" fillcolor="#F5A623" fontcolor="white"];
        l_g [label="Gap Analysis" fillcolor="#D0021B" fontcolor="white"];
        l_s [label="Status Report" fillcolor="#BD10E0" fontcolor="white"];
        l_o [label="Process / Ops" fillcolor="#9B9B9B" fontcolor="white"];
        l_d -> l_p -> l_r -> l_g -> l_s -> l_o [style=invis];
    }

    // ================================================================
    // TIMELINE NODES — all 12 states left to right
    // ================================================================

    s01 [label=<<b>MLP Design</b><br/><font point-size="7">Mar 10</font>> fillcolor="#4A90D9" fontcolor="white" width=1.2];
    s02 [label=<<b>MLP Plan</b><br/><font point-size="7">Mar 10</font>> fillcolor="#7BC67E" fontcolor="white" width=1.2];
    s03 [label=<<b>Observable</b><br/><font point-size="7">Mar 12</font>> fillcolor="#4A90D9" fontcolor="white" width=1.2];
    s04 [label=<<b>Temporal+AVO</b><br/><font point-size="7">Mar 15-16</font>> fillcolor="#4A90D9" fontcolor="white" width=1.2];
    s05 [label=<<b>Build Ops</b><br/><font point-size="7">Mar 16</font>> fillcolor="#9B9B9B" fontcolor="white" width=1.2];
    s06 [label=<<b>RxJS Gap</b><br/><font point-size="7">Mar 17</font>> fillcolor="#D0021B" fontcolor="white" width=1.2];
    s07 [label=<<b>MLP Status</b><br/><font point-size="7">Mar 18</font>> fillcolor="#BD10E0" fontcolor="white" width=1.2];
    s08 [label=<<b>Layers 1-3</b><br/><font point-size="7">Mar 18</font>> fillcolor="#F5A623" fontcolor="white" width=1.2];
    s09 [label=<<b>AST Design</b><br/><font point-size="7">Mar 19</font>> fillcolor="#4A90D9" fontcolor="white" width=1.2];
    s10 [label=<<b>AST Plan</b><br/><font point-size="7">Mar 19</font>> fillcolor="#7BC67E" fontcolor="white" width=1.2];
    s11 [label=<<b>Gap+Fix+Warn</b><br/><font point-size="7">Mar 20</font>> fillcolor="#D0021B" fontcolor="white" width=1.2];
    s12 [label=<<b>Assessment</b><br/><font point-size="7">Mar 20</font>> fillcolor="#F5A623" fontcolor="white" width=1.2 penwidth=3 color="#333333"];

    // Forward flow
    s01 -> s02 [color="#333333" penwidth=2];
    s02 -> s03 [color="#999999" penwidth=1.5 style=dashed label=<<font point-size="6">parallel track</font>>];
    s03 -> s04 [color="#333333" penwidth=2];
    s04 -> s05 [color="#999999" penwidth=1.5];
    s05 -> s06 [color="#999999" penwidth=1.5];
    s06 -> s07 [color="#999999" penwidth=1.5];
    s07 -> s08 [color="#333333" penwidth=2 label=<<font point-size="6">gaps drive</font>>];
    s08 -> s09 [color="#333333" penwidth=2 label=<<font point-size="6">insight</font>>];
    s09 -> s10 [color="#333333" penwidth=2];
    s10 -> s11 [color="#333333" penwidth=2 label=<<font point-size="6">audit</font>>];
    s11 -> s12 [color="#333333" penwidth=2];

    // Back-reference polygons
    br1 [label=<<font point-size="7">refs MLP Design<br/>sections 2-6</font>> shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C" fontsize=7 penwidth=1.5];
    br2 [label=<<font point-size="7">implements MLP Design<br/>sections 3-6</font>> shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C" fontsize=7 penwidth=1.5];
    br3 [label=<<font point-size="7">audits AST Plan<br/>Stage 6</font>> shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C" fontsize=7 penwidth=1.5];
    br4 [label=<<font point-size="7">compares<br/>v0.8.15 goals</font>> shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C" fontsize=7 penwidth=1.5];
    br5 [label=<<font point-size="7">compares<br/>v1.0.0 status</font>> shape=octagon fillcolor="#FFF3E0" color="#E65100" fontcolor="#BF360C" fontsize=7 penwidth=1.5];

    // Back-ref connections
    br1 -> s07 [style=dashed color="#E65100" penwidth=1.5 constraint=false];
    s07 -> s01 [style=dashed color="#E65100" penwidth=1 constraint=false arrowhead=none];

    br2 -> s08 [style=dashed color="#E65100" penwidth=1.5 constraint=false];
    s08 -> s01 [style=dashed color="#E65100" penwidth=1 constraint=false arrowhead=none];

    br3 -> s11 [style=dashed color="#E65100" penwidth=1.5 constraint=false];
    s11 -> s10 [style=dashed color="#E65100" penwidth=1 constraint=false arrowhead=none];

    br4 -> s12 [style=dashed color="#E65100" penwidth=1.5 constraint=false];
    s12 -> s01 [style=dashed color="#E65100" penwidth=1.5 constraint=false arrowhead=none];

    br5 -> s12 [style=dashed color="#E65100" penwidth=1.5 constraint=false];
    s12 -> s07 [style=dashed color="#E65100" penwidth=1 constraint=false arrowhead=none];

    // Iteration brackets (bottom labels)
    iter1 [label=<<font point-size="9" color="#4A90D9"><b>Iteration 1</b><br/>MLP Feasibility<br/>v0.8.15→v1.0.0</font>> shape=plaintext style=""];
    iter2 [label=<<font point-size="9" color="#7BC67E"><b>Iteration 2</b><br/>Observables<br/>v0.9.0→v0.9.8</font>> shape=plaintext style=""];
    iter3 [label=<<font point-size="9" color="#F5A623"><b>Iteration 3</b><br/>Evolution Pipeline<br/>v1.0.0→v1.5.0</font>> shape=plaintext style=""];
    iter4 [label=<<font point-size="9" color="#D0021B"><b>Iteration 4</b><br/>Hardening<br/>v1.5.0→v1.6.0</font>> shape=plaintext style=""];

    // Cycle inset
    subgraph cluster_cycle {
        label=<<b>The Macro Cycle</b>>;
        labeljust=c; fontsize=10; fontcolor="#333333";
        style="rounded,filled"; fillcolor="#FAFAFA"; color="#888888";
        penwidth=1.5; margin=14;

        c_d [label=<<b>DESIGN</b>> fillcolor="#4A90D9" fontcolor="white" shape=ellipse width=1.0 height=0.5 fontsize=8];
        c_p [label=<<b>PLAN</b>> fillcolor="#7BC67E" fontcolor="white" shape=ellipse width=1.0 height=0.5 fontsize=8];
        c_i [label=<<b>IMPLEMENT</b>> fillcolor="#50E3C2" fontcolor="white" shape=ellipse width=1.0 height=0.5 fontsize=8];
        c_r [label=<<b>REVIEW</b>> fillcolor="#F5A623" fontcolor="white" shape=ellipse width=1.0 height=0.5 fontsize=8];

        c_d -> c_p [penwidth=2 color="#333333"];
        c_p -> c_i [penwidth=2 color="#333333"];
        c_i -> c_r [penwidth=2 color="#333333"];
        c_r -> c_d [penwidth=2 color="#333333" label=<<font point-size="6"> gaps feed<br/> next cycle</font>>];
    }

    // Summary table
    summary [
        label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4">
        <tr><td colspan="2" bgcolor="#333333"><font color="white" point-size="9"><b>Document Count</b></font></td></tr>
        <tr><td bgcolor="#4A90D9"><font color="white" point-size="8">Design</font></td><td bgcolor="#EBF5FB"><font point-size="8"> 7 </font></td></tr>
        <tr><td bgcolor="#7BC67E"><font color="white" point-size="8">Plan</font></td><td bgcolor="#F0FFF0"><font point-size="8"> 8 </font></td></tr>
        <tr><td bgcolor="#F5A623"><font color="white" point-size="8">Review</font></td><td bgcolor="#FFF8E1"><font point-size="8"> 2 </font></td></tr>
        <tr><td bgcolor="#D0021B"><font color="white" point-size="8">Gap</font></td><td bgcolor="#FEECEC"><font point-size="8"> 2 </font></td></tr>
        <tr><td bgcolor="#BD10E0"><font color="white" point-size="8">Status</font></td><td bgcolor="#F5EEF8"><font point-size="8"> 1 </font></td></tr>
        </table>>
        shape=plaintext style=""
    ];
}
DOT

echo "=== All dot files generated ==="
ls -1 *.dot

echo ""
echo "=== Rendering PNGs ==="
for f in slide-*.dot; do
    base="${f%.dot}"
    dot -Tpng -Gdpi=150 "$f" -o "${base}.png" 2>&1
    echo "  ${base}.png  $(identify -format '%wx%h' "${base}.png" 2>/dev/null || echo '(no identify)')"
done

echo ""
echo "=== Rendering SVGs ==="
for f in slide-*.dot; do
    base="${f%.dot}"
    dot -Tsvg "$f" -o "${base}.svg" 2>&1
done

echo ""
echo "=== Final listing ==="
ls -lh *.png *.svg 2>/dev/null
