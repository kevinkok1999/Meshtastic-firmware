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
    print(f"V29 portal behavior corpus OK ({len(cases)} cases)")


if __name__ == "__main__":
    main()
