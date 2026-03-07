# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Theme registration for the ETIL MCP client."""

from textual.theme import Theme

ETIL_DARK = Theme(
    name="etil-dark",
    primary="#5e81ac",
    secondary="#88c0d0",
    accent="#b48ead",
    warning="#ebcb8b",
    error="#bf616a",
    success="#a3be8c",
    dark=True,
)

ETIL_LIGHT = Theme(
    name="etil-light",
    primary="#5e81ac",
    secondary="#3b4252",
    accent="#b48ead",
    warning="#d08770",
    error="#bf616a",
    success="#a3be8c",
    dark=False,
)
