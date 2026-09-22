#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import subprocess
import sys

def fail(message: str) -> None:
    raise SystemExit("V16 patch failed: " + message)

def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v16.py <V15-patched WadaMesh checkout>")
    root = pathlib.Path(sys.argv[1]).resolve()
    if not (root / "src/main.cpp").exists() or not (root / "platformio.ini").exists():
        fail("target is not a WadaMesh checkout")
    if "MESH_OFFGRIDNL_V15=1" not in (root / "platformio.ini").read_text():
        fail("V15 base is not present")
    patch = pathlib.Path(__file__).with_name("v16.patch").resolve()
    if not patch.exists():
        fail("v16.patch missing")
    try:
        subprocess.run(["git", "apply", "--check", str(patch)], cwd=root, check=True)
        subprocess.run(["git", "apply", str(patch)], cwd=root, check=True)
    except subprocess.CalledProcessError as exc:
        fail("git apply failed: " + str(exc))
    print("V16 applied: join-priority scan arbitration + one-shot AP hint + single reconnect owner + bounded foreground retries")

if __name__ == "__main__":
    main()
