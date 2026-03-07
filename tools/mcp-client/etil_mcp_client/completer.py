# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Tab-triggered scrollable completion popup for ETIL dictionary words and meta-commands."""

from __future__ import annotations

from textual.widgets import OptionList
from textual.widgets.option_list import Option

# Meta-commands available in the TUI (sorted for consistent prefix matching)
META_COMMANDS: list[str] = sorted([
    "/clear",
    "/download",
    "/help",
    "/info",
    "/load",
    "/log",
    "/logfile",
    "/login",
    "/logjson",
    "/logout",
    "/quit",
    "/reset",
    "/stack",
    "/stats",
    "/upload",
    "/verbose",
    "/whoami",
])


class CompletionOverlay(OptionList):
    """Floating scrollable completion popup.

    Displayed above the Input when Tab is pressed with multiple matches.
    Never takes focus — the Input keeps focus while the popup is visible.
    Up/Down navigate, Tab/Enter accept, Escape dismisses.
    """

    can_focus = False

    def __init__(self) -> None:
        super().__init__(id="completion-popup")
        self._words: list[str] = []

    def set_words(self, words: list[str]) -> None:
        """Update the dictionary word list (sorted for consistent matching)."""
        self._words = sorted(words)

    def get_matches(self, token: str) -> list[str]:
        """Return all words/meta-commands starting with the given prefix."""
        if not token:
            return []
        candidates = META_COMMANDS if token.startswith("/") else self._words
        return [c for c in candidates if c.startswith(token) and c != token]

    def show_completions(self, matches: list[str]) -> None:
        """Populate popup with matches and make visible."""
        self.clear_options()
        self.add_options([Option(m) for m in matches])
        self.highlighted = 0
        self.add_class("visible")

    def hide(self) -> None:
        """Hide the completion popup."""
        self.remove_class("visible")

    @property
    def is_visible(self) -> bool:
        """Whether the popup is currently displayed."""
        return self.has_class("visible")

    def move_highlight(self, delta: int) -> None:
        """Move highlight up (-1) or down (+1), wrapping around."""
        if self.option_count == 0:
            return
        current = self.highlighted if self.highlighted is not None else 0
        self.highlighted = (current + delta) % self.option_count
        self.scroll_to_highlight()

    def selected_text(self) -> str | None:
        """Return the text of the currently highlighted option."""
        idx = self.highlighted
        if idx is None or idx < 0 or idx >= self.option_count:
            return None
        opt = self.get_option_at_index(idx)
        return str(opt.prompt) if opt else None
