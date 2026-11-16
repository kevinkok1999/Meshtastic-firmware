#!/usr/bin/env python3
"""Install/check the V30 direct-join adapter without touching V28 or apply_v30.py.

Native UI wiring is intentionally deferred until the existing channel command
anchor is selected. This script installs a compile-safe, V30-gated parser
header and validates the host contract; it does not create a second history or
message engine.
"""
from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys

BASELINE = "7c492ec1b5157699a39cd96e52f372e5183cddf1"
HEADER = """#pragma once
#if defined(MESH_OFFGRIDNL_V30)
#include <stddef.h>
#include <stdint.h>
#include <ctype.h>

namespace v30_direct_join {
static inline bool hex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}
static inline uint8_t nibble(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  return (uint8_t)(c - 'A' + 10);
}
// Full 128-bit key only: no passphrase or truncation fallback.
static inline bool parseKey(const char* text, size_t len, uint8_t out[16]) {
  if (!text || !out || len != 32) return false;
  for (size_t i = 0; i < 32; ++i) if (!hex(text[i])) return false;
  for (size_t i = 0; i < 16; ++i)
    out[i] = (uint8_t)((nibble(text[i * 2]) << 4) | nibble(text[i * 2 + 1]));
  return true;
}
// V28 native invite locator remains a locator, never a key.
static inline bool parseInvite(const char* text, size_t len) {
  if (!text || len != 9 || text[4] != '-') return false;
  for (size_t i = 0; i < 9; ++i) {
    if (i == 4) continue;
    if (!hex(text[i]) && !(text[i] >= 'A' && text[i] <= 'Z') &&
        !(text[i] >= 'a' && text[i] <= 'z') &&
        !(text[i] >= '0' && text[i] <= '9')) return false;
  }
  return true;
}
}  // namespace v30_direct_join
#endif
"""

def fail(message: str) -> None:
    raise SystemExit("V30 channel patch failed: " + message)

def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_channel_v30.py <V30-patched WadaMesh checkout>")
    root = pathlib.Path(sys.argv[1]).resolve()
    pio = root / "platformio.ini"
    mesh = root / "src/MyMesh.cpp"
    if not pio.exists() or not mesh.exists():
        fail("expected platformio.ini and src/MyMesh.cpp")
    pio_text = pio.read_text(encoding="utf-8")
    start = pio_text.find("[env:LilyGo_TDeck_companion_radio_touch]")
    end = pio_text.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", start)
    if start < 0 or end < 0:
        fail("T-Deck environment missing")
    block = pio_text[start:end]
    if "MESH_OFFGRIDNL_V30=1" not in block:
        fail("MESH_OFFGRIDNL_V30=1 required; apply V30 baseline first")
    target = root / "src/helpers/esp32/V30DirectChannelJoin.h"
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and target.read_text(encoding="utf-8") != HEADER:
        fail("refusing to overwrite a different V30 direct-join header")
    target.write_text(HEADER, encoding="utf-8")
    print("V30 direct-join adapter installed; existing native lookup/history remains the integration path.")

if __name__ == "__main__":
    main()
