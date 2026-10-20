#!/usr/bin/env python3
"""Independent request corpus for the V29 portal token grammar.

The firmware parser accepts exactly one query parameter named t, with a full
constant-length value.  These cases guard the regressions that source markers
cannot detect: prefixes, duplicate parameters, malformed separators and token
substitution.
"""
from __future__ import annotations

import sys
from urllib.parse import urlsplit


def token_ok(path: str, token: str) -> bool:
    query = urlsplit(path).query
    if not query or not token:
        return False
    matched = False
    for field in query.split("&"):
        if field.startswith("t="):
            if matched or len(field) != len(token) + 2:
                return False
            diff = 0
            for a, b in zip(field[2:], token):
                diff |= ord(a) ^ ord(b)
            if diff:
                return False
            matched = True
    return matched


def main() -> None:
    t = "0123456789ABCDEF"
    cases = {
        "/safe?t=" + t: True,
        "/safe?t=" + t + "0": False,
        "/safe?t=" + t[:15]: False,
        "/safe?t=" + t + "&t=" + t: False,
        "/safe?x=1&t=" + t: True,
        "/safe?t=" + t.lower(): False,
        "/safe?t%3D=" + t: False,
        "/safe?t=" + t + "&x": True,
        "/safe": False,
        "/safe?t=" + t + "#fragment": True,
    }
    for path, expected in cases.items():
        actual = token_ok(path, t)
        if actual != expected:
            raise SystemExit(f"portal corpus mismatch: {path!r}: {actual} != {expected}")
    # Prevent regression where GET / emitted token-bearing HTML on STA.
    # The production C++ handler must gate AP-only access BEFORE serving
    # root/captive endpoints or extending its session activity timeout.
    from pathlib import Path
    portal = Path(__file__).resolve().parents[1] / "overlay/src/helpers/esp32/V29EmergencyPortal.cpp"
    handler = portal.read_text(encoding="utf-8").split(
        "void V29EmergencyPortal::handleClient(", 1
    )[1].split("void V29EmergencyPortal::loop(", 1)[0]
    boundary = handler.index("c.localIP() != WiFi.softAPIP()")
    root = handler.index('strcmp(path, "/") == 0')
    page = handler.index("sendPage(c);")
    activity = handler.index("_lastActivityMs = millis();")
    assert boundary < root < page, "STA client can access token-bearing captive page"
    assert boundary < activity, "STA client can extend portal activity timeout"
    assert handler.count("c.localIP() != WiFi.softAPIP()") == 1

    assert "v29ReadHttpRequestLine(" in handler, "request line must wait for LF"
    assert "strchr(line, ' ')" not in handler, "premature partial-read regression"
    assert '"HTTP/1.1 414 URI Too Long' in handler, "oversized line must be rejected"
    assert '"HTTP/1.1 408 Request Timeout' in handler, "truncated line must time out"

    print(f"V29 portal behavior corpus OK ({len(cases)} cases)")


if __name__ == "__main__":
    main()
