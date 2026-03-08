# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Render MCP tool JSON responses into markdown for the help browser."""

from __future__ import annotations

import json

# Display order for categories (unlisted categories sort to end alphabetically)

# Display labels for categories
CATEGORY_ORDER = [
    "arithmetic",
    "stack",
    "comparison",
    "logic",
    "compiling",
    "control",
    "i/o",
    "memory",
    "math",
    "string",
    "map",
    "array",
    "byte-array",
    "json",
    "matrix",
    "lvfs",
    "file-io-async",
    "file-io-sync",
    "http",
    "mongodb",
    "time",
    "system",
    "input",
    "dictionary",
    "metadata",
    "help",
    "debug",
    "execution",
    "defining",
    "conversion",
]
CATEGORY_LABELS = {
    "arithmetic": "Arithmetic",
    "stack": "Stack",
    "comparison": "Comparison",
    "logic": "Logic",
    "i/o": "I/O",
    "memory": "Memory",
    "math": "Math",
    "string": "String",
    "array": "Array",
    "byte-array": "Byte Array",
    "lvfs": "Virtual Filesystem",
    "file-io-async": "File I/O (Async)",
    "file-io-sync": "File I/O (Sync)",
    "http": "HTTP Client",
    "time": "Time",
    "system": "System",
    "input": "Input Reading",
    "dictionary": "Dictionary",
    "metadata": "Metadata",
    "help": "Help",
    "debug": "Debug",
    "control": "Control Flow",
    "compiling": "Compiling",
    "execution": "Execution Tokens",
    "defining": "Defining Words",
    "conversion": "Type Conversion",
    "json": "JSON",
    "matrix": "Matrix (Linear Algebra)",
    "mongodb": "MongoDB",
}

# Meta-commands available in the TUI
META_COMMANDS = [
    ("/stack", "Show current stack state"),
    ("/reset", "Reset interpreter state"),
    ("/stats", "Show session statistics"),
    ("/info \\<word\\>", "Open help page for a word"),
    ("/load \\<path\\>", "Load a local .til file into the interpreter"),
    ("/upload \\<local\\> [remote]", "Upload a file to the server (no execute)"),
    ("/download \\<remote\\> [local]", "Download a file from the server"),
    ("/login [provider]", "OAuth login (default: github)"),
    ("/logout", "Clear JWT, revert to API key auth"),
    ("/whoami", "Show current authentication state"),
    ("/logfile [path]", "Toggle plain-text session log"),
    ("/logjson [path]", "Toggle JSON session log"),
    ("/log", "Toggle both text and JSON logs"),
    ("/verbose [on\\|off]", "Toggle verbose JSON in server panel"),
    ("/help [word]", "Open help browser (optionally at word)"),
    ("/clear", "Clear server output panel"),
    ("/quit", "Exit the client"),
]

META_KEYBINDINGS = [
    ("F1", "Open help browser"),
    ("F2", "Cycle layout (both / IO only / JSON-RPC only)"),
    ("F3", "Toggle notification fullscreen"),
    ("F4", "Toggle notification scroll (top/bottom)"),
    ("Tab", "Cycle focus between panels"),
    ("Ctrl+Q", "Quit"),
    ("Ctrl+D", "Dismiss notification"),
    ("Ctrl+L", "Clear output"),
    ("Ctrl+R", "Reverse history search"),
    ("Up/Down", "Navigate command history"),
    ("Escape", "Focus command input / cancel search"),
]


def render_breadcrumbs(
    page_type: str,
    page_arg: str | None = None,
    category: str | None = None,
) -> str:
    """Render breadcrumb navigation for a help page.

    Args:
        page_type: "index", "category", or "word".
        page_arg: Category name or word name.
        category: Word's category (only for word pages).
    """
    if page_type == "index":
        return "**Index**\n"
    elif page_type == "category":
        label = CATEGORY_LABELS.get(page_arg or "", (page_arg or "").title())
        return f"[Index](etil://index) > **{label}**\n"
    elif page_type == "word":
        display = (page_arg or "").replace("`", "\\`")
        if category:
            cat_label = CATEGORY_LABELS.get(category, category.title())
            return (
                f"[Index](etil://index) > "
                f"[{cat_label}](etil://category/{category}) > "
                f"**`{display}`**\n"
            )
        else:
            return f"[Index](etil://index) > **`{display}`**\n"
    return ""


def render_meta_commands() -> str:
    """Render the TUI meta-commands as a markdown section."""
    lines = [
        "## TUI Commands",
        "",
        "| Command | Description |",
        "|---------|-------------|",
    ]
    for cmd, desc in META_COMMANDS:
        lines.append(f"| `{cmd}` | {desc} |")
    lines.append("")
    lines.append("**Keybindings:**")
    lines.append("")
    lines.append("| Key | Action |")
    lines.append("|-----|--------|")
    for key, action in META_KEYBINDINGS:
        lines.append(f"| `{key}` | {action} |")
    lines.append("")
    return "\n".join(lines)


def _word_link(name: str) -> str:
    """Generate a markdown link for a word."""
    # URL-encode the name for the URI, display name in code span
    from urllib.parse import quote
    encoded = quote(name, safe="")
    # Use html escape for backtick-unfriendly names containing quotes
    display = name.replace("`", "\\`").replace("|", "\\|")
    return f"[`{display}`](etil://word/{encoded})"


def _category_link(cat: str) -> str:
    """Generate a markdown link for a category."""
    label = CATEGORY_LABELS.get(cat, cat.title())
    return f"[{label}](etil://category/{cat})"


def _category_sort_key(cat: str) -> tuple[int, str]:
    """Sort key: ordered categories first, then alphabetical."""
    try:
        return (CATEGORY_ORDER.index(cat), cat)
    except ValueError:
        return (len(CATEGORY_ORDER), cat)


def _extract_tool_json(response: dict) -> dict | None:
    """Parse the double-encoded JSON from an MCP tool response.

    MCP tool responses have structure:
        {"result": {"content": [{"type": "text", "text": "<json-string>"}]}}
    """
    try:
        result = response.get("result", response)
        content_list = result.get("content", [])
        if not content_list:
            return None
        text = content_list[0].get("text", "")
        return json.loads(text) if text else None
    except (json.JSONDecodeError, KeyError, IndexError, TypeError):
        return None


def render_main_index(words: list[dict]) -> str:
    """Render the top-level help index page.

    Args:
        words: List of word dicts from list_words tool, e.g.
               [{"name": "+", "description": "Add two numbers", "category": "arithmetic"}, ...]
    """
    # Group words by category
    categories: dict[str, list[dict]] = {}
    uncategorized: list[dict] = []

    for w in words:
        cat = w.get("category", "")
        if cat:
            categories.setdefault(cat, []).append(w)
        else:
            uncategorized.append(w)

    lines = [
        "# ETIL Help Index",
        "",
        f"**{len(words)} words** across **{len(categories)}** categories.",
        "",
        render_meta_commands(),
    ]

    # Render each category
    sorted_cats = sorted(categories.keys(), key=_category_sort_key)
    for cat in sorted_cats:
        cat_words = sorted(categories[cat], key=lambda w: w["name"])
        label = CATEGORY_LABELS.get(cat, cat.title())
        lines.append(f"## {_category_link(cat)}")
        lines.append("")
        lines.append("| Word | Stack Effect | Description |")
        lines.append("|------|-------------|-------------|")
        for w in cat_words:
            desc = w.get("description", "")
            effect = w.get("stackEffect", "")
            effect_cell = f"`{effect.replace('|', '\\|')}`" if effect else ""
            lines.append(f"| {_word_link(w['name'])} | {effect_cell} | {desc.replace('|', '\\|')} |")
        lines.append("")

    # Uncategorized words (if any)
    if uncategorized:
        lines.append("## Other")
        lines.append("")
        lines.append("| Word | Stack Effect | Description |")
        lines.append("|------|-------------|-------------|")
        for w in sorted(uncategorized, key=lambda w: w["name"]):
            desc = w.get("description", "")
            effect = w.get("stackEffect", "")
            effect_cell = f"`{effect.replace('|', '\\|')}`" if effect else ""
            lines.append(f"| {_word_link(w['name'])} | {effect_cell} | {desc.replace('|', '\\|')} |")
        lines.append("")

    return "\n".join(lines)


def render_category_index(words: list[dict], category: str) -> str:
    """Render a single category page.

    Args:
        words: Full word list (will be filtered).
        category: Category name to filter by.
    """
    label = CATEGORY_LABELS.get(category, category.title())
    cat_words = [w for w in words if w.get("category") == category]
    cat_words.sort(key=lambda w: w["name"])

    lines = [
        f"# {label}",
        "",
        f"*{len(cat_words)} words in this category.* "
        "[Back to Index](etil://index)",
        "",
        "| Word | Description | Stack Effect |",
        "|------|-------------|-------------|",
    ]

    for w in cat_words:
        desc = w.get("description", "")
        effect = w.get("stackEffect", "")
        effect_cell = f"`{effect.replace('|', '\\|')}`" if effect else ""
        lines.append(f"| {_word_link(w['name'])} | {desc.replace('|', '\\|')} | {effect_cell} |")

    lines.append("")
    return "\n".join(lines)


def _normalize_example_text(text: str) -> str:
    """Convert literal escape sequences to real characters for display.

    Metadata from help.til stores \\n and \\" as literal two-character
    sequences since string-read-delim preserves backslashes.
    """
    return text.replace("\\n", "\n").replace('\\"', '"')


def extract_runnable_code(text: str) -> str:
    """Extract runnable code from example text.

    Converts literal \\n to real newlines, then strips output annotation
    lines (starting with '=> ').
    """
    normalized = _normalize_example_text(text)
    lines = normalized.split("\n")
    code_lines = [line for line in lines if not line.startswith("=> ")]
    return "\n".join(code_lines).strip()


def render_word_help(
    word_info: dict | None,
    word_name: str,
    all_words: list[dict] | None = None,
    example_run: dict | None = None,
    expanded_impls: set[int] | None = None,
) -> str:
    """Render a manpage for a single word.

    Args:
        word_info: Parsed JSON from get_word_info tool (may be None for handler words).
        word_name: The word name (used for handler word fallback).
        all_words: Full word list for generating See Also links.
    """
    if word_info is None:
        return f"# {word_name}\n\nNo information available for `{word_name}`."

    meta = word_info.get("metadata", {})
    description = _meta_content(meta, "description")
    stack_effect = _meta_content(meta, "stack-effect")
    category = _meta_content(meta, "category")

    lines = [f"# `{word_name}`", ""]

    # Category badge
    if category:
        lines.append(f"**Category:** {_category_link(category)}")
        lines.append("")

    # Description
    if description:
        lines.append(f"{description}")
        lines.append("")

    # Stack effect
    if stack_effect:
        lines.append("### Stack Effect")
        lines.append("")
        lines.append(f"```")
        lines.append(stack_effect)
        lines.append(f"```")
        lines.append("")

    # Examples from metadata
    examples_text = _meta_content(meta, "examples")
    if examples_text:
        normalized = _normalize_example_text(examples_text)
        lines.append("### Examples")
        lines.append("")
        if example_run is not None:
            # Live output available — show code without annotations + real output
            code_only = "\n".join(
                line for line in normalized.split("\n")
                if not line.startswith("=> ")
            )
            lines.append("```forth")
            lines.append(code_only)
            lines.append("```")
            lines.append("")
            output = (example_run.get("output", "") or "").rstrip()
            errors = (example_run.get("errors", "") or "").rstrip()
            stack = example_run.get("stack", [])
            if output:
                lines.append("Output:")
                lines.append("```")
                lines.append(output)
                lines.append("```")
                lines.append("")
            if errors:
                lines.append("Error:")
                lines.append("```")
                lines.append(errors)
                lines.append("```")
                lines.append("")
            if stack and not output and not errors:
                lines.append(f"Stack: `{' '.join(str(s) for s in stack)}`")
                lines.append("")
        else:
            # No run result — show code with annotations as-is
            lines.append("```forth")
            lines.append(normalized)
            lines.append("```")
            lines.append("")

    # Implementations (collapsible tree with per-impl detail)
    if word_info:
        impls = word_info.get("implementations", [])
        if impls:
            expanded = expanded_impls or set()
            lines.append("### Implementations")
            lines.append("")

            for idx, impl in enumerate(impls):
                name = impl.get("name", "?")
                gen = impl.get("generation", 0)
                weight = impl.get("weight", 1.0)
                profile = impl.get("profile", {})
                calls = profile.get("totalCalls", 0)
                success = profile.get("successRate", 0.0)
                success_pct = f"{success * 100:.0f}%" if calls > 0 else "\u2014"

                is_expanded = idx in expanded
                indicator = "\u25bc" if is_expanded else "\u25b6"
                toggle = f"[{indicator}](etil://impl-toggle/{idx})"

                lines.append(
                    f"{toggle} **`{name}`** \u2014 "
                    f"Gen {gen} | Weight {weight:.2f} | "
                    f"Calls {calls} | Success {success_pct}"
                )
                lines.append("")

                if is_expanded:
                    # Build detail rows
                    rows: list[tuple[str, str]] = []
                    impl_id = impl.get("id", None)
                    if impl_id is not None:
                        rows.append(("ID", str(impl_id)))
                    rows.append(("Generation", str(gen)))
                    rows.append(("Weight", f"{weight:.4f}"))
                    if calls > 0:
                        mean_ns = profile.get("meanDurationNs", 0)
                        if mean_ns >= 1_000_000:
                            rows.append(("Mean Duration", f"{mean_ns / 1_000_000:.2f} ms"))
                        elif mean_ns >= 1_000:
                            rows.append(("Mean Duration", f"{mean_ns / 1_000:.1f} \u00b5s"))
                        else:
                            rows.append(("Mean Duration", f"{mean_ns} ns"))
                        rows.append(("Total Calls", str(calls)))
                        rows.append(("Success Rate", success_pct))

                    # Type signature
                    sig = impl.get("signature", {})
                    inputs = sig.get("inputs", [])
                    outputs = sig.get("outputs", [])
                    all_unknown = all(t == "unknown" for t in inputs) and all(
                        t == "unknown" for t in outputs
                    )
                    if (inputs or outputs) and not all_unknown:
                        in_str = ", ".join(inputs) if inputs else "\u2014"
                        out_str = ", ".join(outputs) if outputs else "\u2014"
                        rows.append(("Signature", f"{in_str} \u2192 {out_str}"))

                    # Impl-level metadata
                    impl_meta = impl.get("metadata", {})
                    for key, entry in sorted(impl_meta.items()):
                        if isinstance(entry, dict):
                            content = entry.get("content", "")
                            display = (content[:80] + "...") if len(content) > 80 else content
                            display = display.replace("|", "\\|").replace("\n", " ")
                            rows.append((key, display))

                    if rows:
                        lines.append("| Property | Value |")
                        lines.append("|----------|-------|")
                        for prop, val in rows:
                            lines.append(f"| {prop} | {val} |")
                        lines.append("")

            lines.append("")

    # See Also: other words in the same category
    if category and all_words:
        same_cat = sorted(
            w["name"]
            for w in all_words
            if w.get("category") == category and w["name"] != word_name
        )

        if same_cat:
            lines.append("### See Also")
            lines.append("")
            links = [_word_link(name) for name in same_cat]
            lines.append(" ".join(links))
            lines.append("")

    # Navigation footer
    lines.append("---")
    lines.append("[Back to Index](etil://index)")

    return "\n".join(lines)


def render_search_results(words: list[dict], query: str) -> str:
    """Render filtered search results as a markdown table.

    Args:
        words: Full word list (from list_words + handler words).
        query: Case-insensitive substring to match against name or description.
    """
    q = query.lower()

    matches = [
        w for w in words
        if q in w["name"].lower() or q in w.get("description", "").lower()
    ]
    matches.sort(key=lambda w: w["name"])

    lines = [
        f"# Search: \"{query}\"",
        "",
        f"*{len(matches)} result{'s' if len(matches) != 1 else ''}.*",
        "",
    ]

    if matches:
        lines.append("| Word | Stack Effect | Category | Description |")
        lines.append("|------|-------------|----------|-------------|")
        for w in matches:
            desc = w.get("description", "")
            effect = w.get("stackEffect", "")
            effect_cell = f"`{effect.replace('|', '\\|')}`" if effect else ""
            cat = w.get("category", "")
            cat_cell = _category_link(cat) if cat else ""
            lines.append(
                f"| {_word_link(w['name'])} | {effect_cell} | {cat_cell} | {desc.replace('|', '\\|')} |"
            )
        lines.append("")
    else:
        lines.append("No words match your search.")
        lines.append("")

    return "\n".join(lines)


def _meta_content(metadata: dict, key: str) -> str:
    """Extract content string from metadata dict.

    Metadata format from word_concept_to_json:
        {"description": {"key": "description", "format": "text", "content": "..."}}
    """
    entry = metadata.get(key, {})
    if isinstance(entry, dict):
        return entry.get("content", "")
    return ""
