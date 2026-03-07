# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Full-screen help browser for ETIL words."""

from __future__ import annotations

import logging
from urllib.parse import unquote

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import VerticalScroll
from textual.screen import Screen
from textual.widgets import Footer, Header, Input, Markdown

from ..config import ClientConfig
from ..help_renderer import (
    _extract_tool_json,
    extract_runnable_code,
    render_breadcrumbs,
    render_category_index,
    render_main_index,
    render_search_results,
    render_word_help,
)
from ..http_transport import HttpStreamableTransport
from ..protocol import McpProtocol
from ..transport import McpTransport

log = logging.getLogger(__name__)


class HelpScreen(Screen):
    """Full-screen help browser with markdown rendering and navigation."""

    BINDINGS = [
        Binding("escape", "close_or_search", "Close"),
        Binding("q", "close", "Close", show=False),
        Binding("backspace", "back", "Back"),
        Binding("i", "go_index", "Index"),
        Binding("n", "next_word", "Next", show=False),
        Binding("p", "prev_word", "Prev", show=False),
        Binding("slash", "open_search", "Search", show=True),
    ]

    def __init__(
        self,
        protocol: McpProtocol,
        config: ClientConfig | None = None,
        initial_word: str | None = None,
        **kwargs,
    ) -> None:
        super().__init__(**kwargs)
        self._protocol = protocol
        self._config = config
        self._initial_word = initial_word
        self._words_cache: list[dict] | None = None
        self._word_info_cache: dict[str, dict | None] = {}
        self._example_cache: dict[str, dict | None] = {}
        self._history: list[tuple[str, str | None]] = []
        # Current page: ("index", None), ("category", cat), ("word", name)
        self._current_page: tuple[str, str | None] = ("index", None)
        self._searching = False
        self._pre_search_page: tuple[str, str | None] | None = None
        # Expanded implementation indices on word detail page
        self._expanded_impls: set[int] = set()
        # Sandbox for isolated example execution
        self._sandbox_transport: McpTransport | None = None
        self._sandbox_protocol: McpProtocol | None = None

    def compose(self) -> ComposeResult:
        yield Header()
        yield Input(
            placeholder="Search words...",
            id="help-search",
            classes="hidden",
        )
        with VerticalScroll(id="help-scroll"):
            yield Markdown("*Loading...*", id="help-md", open_links=False)
        yield Footer()

    @property
    def md_widget(self) -> Markdown:
        return self.query_one("#help-md", Markdown)

    @property
    def scroll_container(self) -> VerticalScroll:
        return self.query_one("#help-scroll", VerticalScroll)

    async def on_mount(self) -> None:
        """Fetch word list and render initial page."""
        await self._fetch_words()

        if self._initial_word:
            await self._navigate_to("word", self._initial_word, push_history=False)
        else:
            await self._navigate_to("index", None, push_history=False)

        self.scroll_container.focus()

    async def _fetch_words(self) -> None:
        """Fetch and cache the full word list (one MCP call per session)."""
        if self._words_cache is not None:
            return
        try:
            response = await self._protocol.call_tool("list_words")
            data = _extract_tool_json(response)
            if data and "words" in data:
                self._words_cache = data["words"]
            else:
                self._words_cache = []
        except Exception as exc:
            log.error("Failed to fetch word list: %s", exc)
            self._words_cache = []

    async def _navigate_to(
        self,
        page_type: str,
        page_arg: str | None,
        push_history: bool = True,
    ) -> None:
        """Navigate to a page and optionally push current to history."""
        if push_history:
            self._history.append(self._current_page)

        # Clear expanded impls when navigating to a different word
        if page_type != "word" or page_arg != self._current_page[1]:
            self._expanded_impls.clear()

        self._current_page = (page_type, page_arg)
        await self._render_current_page()

    def _word_category(self, word_name: str) -> str | None:
        """Look up a word's category from cache."""
        if self._words_cache:
            for w in self._words_cache:
                if w["name"] == word_name:
                    cat = w.get("category", "")
                    if cat:
                        return cat
        return None

    async def _render_current_page(self, scroll_to_top: bool = True) -> None:
        """Render the current page into the Markdown widget."""
        page_type, page_arg = self._current_page
        words = self._words_cache or []

        if page_type == "index":
            breadcrumb = render_breadcrumbs("index")
            md = render_main_index(words)
            self.sub_title = "Index"
        elif page_type == "category":
            breadcrumb = render_breadcrumbs("category", page_arg)
            md = render_category_index(words, page_arg or "")
            from ..help_renderer import CATEGORY_LABELS
            label = CATEGORY_LABELS.get(page_arg or "", (page_arg or "").title())
            self.sub_title = label
        elif page_type == "word":
            cat = self._word_category(page_arg or "")
            breadcrumb = render_breadcrumbs("word", page_arg, cat)
            word_info = await self._fetch_word_info(page_arg or "")
            example_run = await self._run_word_example(word_info, page_arg or "")
            md = render_word_help(
                word_info, page_arg or "", words,
                example_run=example_run,
                expanded_impls=self._expanded_impls,
            )
            self.sub_title = page_arg or ""
        else:
            breadcrumb = ""
            md = "Unknown page."
            self.sub_title = "Help"

        self.title = "ETIL Help"
        full_md = breadcrumb + "\n" + md if breadcrumb else md
        await self.md_widget.update(full_md)
        if scroll_to_top:
            self.scroll_container.scroll_home(animate=False)

    async def _fetch_word_info(self, word: str) -> dict | None:
        """Fetch detailed word info from the MCP server (cached per session)."""
        if word in self._word_info_cache:
            return self._word_info_cache[word]
        try:
            response = await self._protocol.call_tool(
                "get_word_info", {"name": word}
            )
            # Check for error response (handler words like ":" are not in dictionary)
            result = response.get("result", response)
            if isinstance(result, dict) and result.get("isError"):
                self._word_info_cache[word] = None
                return None
            data = _extract_tool_json(response)
            self._word_info_cache[word] = data
            return data
        except Exception as exc:
            log.error("Failed to fetch word info for '%s': %s", word, exc)
            return None

    # ── Sandbox for isolated example execution ────────────────

    async def _ensure_sandbox(self) -> McpProtocol | None:
        """Lazily start a sandbox and return its protocol.

        Creates a separate HTTP session on the same remote server so
        examples don't pollute the user's session.
        """
        if self._sandbox_protocol is not None:
            return self._sandbox_protocol
        if self._config is None:
            return None
        try:
            self._sandbox_transport = HttpStreamableTransport(
                url=self._config.http_url,
                api_key=self._config.http_api_key,
                connection_name=self._config.connection_name,
            )
            self._sandbox_protocol = McpProtocol(self._sandbox_transport)
            # No JSON-RPC logging for sandbox (silent)
            await self._sandbox_transport.start()
            await self._sandbox_protocol.initialize()
            log.info("Help sandbox started (%s)", self._sandbox_transport.transport_label)
            return self._sandbox_protocol
        except Exception as exc:
            log.error("Failed to start help sandbox: %s", exc)
            self._sandbox_transport = None
            self._sandbox_protocol = None
            return None

    async def _shutdown_sandbox(self) -> None:
        """Stop the sandbox container."""
        if self._sandbox_protocol:
            self._sandbox_protocol.cancel_all()
            self._sandbox_protocol = None
        if self._sandbox_transport:
            try:
                await self._sandbox_transport.shutdown()
            except Exception as exc:
                log.error("Error shutting down help sandbox: %s", exc)
            self._sandbox_transport = None

    def _get_example_text(
        self, word_info: dict | None, word_name: str,
    ) -> str:
        """Extract raw example text from word info."""
        if word_info:
            meta = word_info.get("metadata", {})
            entry = meta.get("examples", {})
            if isinstance(entry, dict):
                return entry.get("content", "")
        return ""

    async def _run_word_example(
        self, word_info: dict | None, word_name: str,
    ) -> dict | None:
        """Run a word's example in the sandbox and return the result.

        Results are cached per word — examples are deterministic.
        Uses reset before each example for a clean interpreter state.
        Falls back to None if sandbox unavailable.
        """
        if word_name in self._example_cache:
            return self._example_cache[word_name]
        raw = self._get_example_text(word_info, word_name)
        if not raw:
            self._example_cache[word_name] = None
            return None
        code = extract_runnable_code(raw)
        if not code:
            self._example_cache[word_name] = None
            return None
        sandbox = await self._ensure_sandbox()
        if sandbox is None:
            return None
        try:
            # Reset sandbox to clean state before each example
            await sandbox.call_tool("reset")
            response = await sandbox.call_tool(
                "interpret", {"code": code},
            )
            data = _extract_tool_json(response)
            self._example_cache[word_name] = data
            return data
        except Exception as exc:
            log.error("Failed to run example for '%s': %s", word_name, exc)
            return None

    # ── Link handling ────────────────────────────────────────

    async def on_markdown_link_clicked(self, event: Markdown.LinkClicked) -> None:
        """Intercept etil:// links for in-browser navigation."""
        href = event.href

        # Close search UI if active (navigating away from results)
        if self._searching:
            self._searching = False
            self.search_input.add_class("hidden")
            self.search_input.value = ""
            self._pre_search_page = None

        if href.startswith("etil://word/"):
            word_name = unquote(href[len("etil://word/"):])
            await self._navigate_to("word", word_name)
        elif href.startswith("etil://category/"):
            category = unquote(href[len("etil://category/"):])
            await self._navigate_to("category", category)
        elif href == "etil://index":
            await self._navigate_to("index", None)
        elif href.startswith("etil://impl-toggle/"):
            try:
                idx = int(href[len("etil://impl-toggle/"):])
                if idx in self._expanded_impls:
                    self._expanded_impls.discard(idx)
                else:
                    self._expanded_impls.add(idx)
                await self._render_current_page(scroll_to_top=False)
            except (ValueError, IndexError):
                pass

    # ── Search ──────────────────────────────────────────────

    @property
    def search_input(self) -> Input:
        return self.query_one("#help-search", Input)

    def action_open_search(self) -> None:
        """Show the search input and focus it."""
        if self._searching:
            return
        self._searching = True
        self._pre_search_page = self._current_page
        si = self.search_input
        si.remove_class("hidden")
        si.value = ""
        si.focus()

    async def _close_search(self) -> None:
        """Hide search and restore previous page."""
        if not self._searching:
            return
        self._searching = False
        si = self.search_input
        si.add_class("hidden")
        si.value = ""
        if self._pre_search_page:
            self._current_page = self._pre_search_page
            self._pre_search_page = None
            await self._render_current_page()

    async def on_input_changed(self, event: Input.Changed) -> None:
        """Filter words as the user types in search."""
        if not self._searching or event.input.id != "help-search":
            return
        query = event.value.strip()
        words = self._words_cache or []
        if query:
            md = render_search_results(words, query)
        else:
            md = render_search_results(words, "")
        await self.md_widget.update(md)
        self.scroll_container.scroll_home(animate=False)

    # ── Actions ──────────────────────────────────────────────

    async def action_close_or_search(self) -> None:
        """Close search if active, otherwise close the screen."""
        if self._searching:
            await self._close_search()
        else:
            await self._shutdown_sandbox()
            self.app.pop_screen()

    async def action_close(self) -> None:
        """Return to the REPL."""
        await self._shutdown_sandbox()
        self.app.pop_screen()

    async def action_back(self) -> None:
        """Go back in navigation history."""
        if self._history:
            self._current_page = self._history.pop()
            await self._render_current_page()

    async def action_go_index(self) -> None:
        """Jump to the main index."""
        await self._navigate_to("index", None)

    def _sorted_category_words(self, category: str) -> list[str]:
        """Get sorted list of word names in a category."""
        names: list[str] = []
        if self._words_cache:
            for w in self._words_cache:
                if w.get("category") == category:
                    names.append(w["name"])
        names.sort()
        return names

    async def action_next_word(self) -> None:
        """Navigate to next word in same category (wraps)."""
        page_type, page_arg = self._current_page
        if page_type != "word" or not page_arg:
            return
        cat = self._word_category(page_arg)
        if not cat:
            return
        words = self._sorted_category_words(cat)
        if not words:
            return
        try:
            idx = words.index(page_arg)
        except ValueError:
            return
        next_idx = (idx + 1) % len(words)
        await self._navigate_to("word", words[next_idx])

    async def action_prev_word(self) -> None:
        """Navigate to previous word in same category (wraps)."""
        page_type, page_arg = self._current_page
        if page_type != "word" or not page_arg:
            return
        cat = self._word_category(page_arg)
        if not cat:
            return
        words = self._sorted_category_words(cat)
        if not words:
            return
        try:
            idx = words.index(page_arg)
        except ValueError:
            return
        prev_idx = (idx - 1) % len(words)
        await self._navigate_to("word", words[prev_idx])
