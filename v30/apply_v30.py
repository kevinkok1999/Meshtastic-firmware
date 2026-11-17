#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import shutil
import sys
from apply_browser_framing_v30 import apply_browser_framing
from apply_portal_v30 import apply as apply_portal
from apply_channel_v30 import apply as apply_channel
from apply_map_guard_v30 import apply as apply_map_guard

WADAMESH_SHA = "6fe6b9f06332708b6ed0cc78958017d0dcc9e35d"
MESHCORE_SHA = "edd7ed47f42acf6c64c9fcac551050fdbd531a09"
CORE_TAG = "https://github.com/ALLFATHER-BV/meshcomod.git#core-v1.17.4"
CORE_PIN = "https://github.com/ALLFATHER-BV/meshcomod.git#" + MESHCORE_SHA


def fail(message: str) -> None:
    raise SystemExit("V30 patch failed: " + message)


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v30.py <V11-through-V29 WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    here = pathlib.Path(__file__).resolve().parent
    required = [
        root / "platformio.ini",
        root / "src/MyMesh.h",
        root / "src/MyMesh.cpp",
        root / "src/main.cpp",
    ]
    for path in required:
        if not path.exists():
            fail("missing " + str(path))

    pio_path = required[0]
    pio = pio_path.read_text(encoding="utf-8")
    start = pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    end = pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", start)
    if start < 0 or end < 0:
        fail("T-Deck environment missing")

    block = pio[start:end]
    if "MESH_OFFGRIDNL_V29=1" not in block:
        fail("V29 baseline must be applied first")
    if "MESH_OFFGRIDNL_V30=1" not in block:
        block = once(
            block,
            "  -D MESH_OFFGRIDNL_V29=1\n",
            "  -D MESH_OFFGRIDNL_V29=1\n  -D MESH_OFFGRIDNL_V30=1\n",
            "V30 build flag",
        )
        pio = pio[:start] + block + pio[end:]

    if CORE_TAG in pio:
        pio = pio.replace(CORE_TAG, CORE_PIN)
    # Patch the V30 checkout only, never the historical V28/V29 sources.
    apply_browser_framing(root)
    pio_path.write_text(pio, encoding="utf-8")

    overlay = here / "overlay"
    for src in overlay.rglob("*"):
        if src.is_file():
            dst = root / src.relative_to(overlay)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)

    apply_portal(root)
    apply_channel(root)
    apply_map_guard(root)

    # V30 sender/receiver integration stays disabled until the real dispatcher
    # completion hook exists. The copied helper is host-tested only; V29 remains
    # the firmware fallback and no experimental path is injected here.
    print("V30 overlay applied: WadaMesh=" + WADAMESH_SHA + " MeshCore=" + MESHCORE_SHA)


if __name__ == "__main__":
    main()
