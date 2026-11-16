#!/usr/bin/env python3
"""Exact-source V30-only hardening for the pinned terminal WebSocket parser.

Framing validation only: does not authenticate a browser session or grant
access to channel secrets. Unknown baseline must fail before changes.
"""
from pathlib import Path

def patch_browser_framing(source: str) -> str:
    here = Path(__file__).resolve().parent
    old = (here / "tests/browser_frames/term_fixture.inc").read_text()
    new = (here / "patches/term_v30.inc").read_text()
    if source.count(new) == 1 and source.count(old) == 0:
        return source
    if source.count(old) != 1 or source.count(new) != 0:
        raise ValueError("V30 WebSocket parser anchor mismatch")
    return source.replace(old, new, 1)

def apply_browser_framing(root: Path) -> None:
    path = root / "src/helpers/esp32/WebSocketCompanionServer.cpp"
    original = path.read_text()
    updated = patch_browser_framing(original)
    if updated != original:
        path.write_text(updated)
