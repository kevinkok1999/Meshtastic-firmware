#!/usr/bin/env python3
"""F1 packet-routing contract corpus.

The old V29 sender uses sendFlood for emergency RAW, so this corpus is RED
until the V30 overlay is applied. The fixture is tied to the immutable
MeshCore source commit and exercises the dispatch boundary instead of merely
checking a mock.
"""
from __future__ import annotations

import pathlib
import subprocess
import tempfile
import urllib.request

REPO = pathlib.Path(__file__).resolve().parents[2]
TEST_CPP = pathlib.Path(__file__).with_name("test_transport.cpp")
MESHCORE_SHA = "edd7ed47f42acf6c64c9fcac551050fdbd531a09"
MESHCORE_URL = (
    "https://raw.githubusercontent.com/ALLFATHER-BV/meshcomod/"
    + MESHCORE_SHA + "/src/Mesh.cpp"
)


def require(text: str, marker: str) -> None:
    if marker not in text:
        raise AssertionError(f"actual-core fixture missing {marker!r}")


def main() -> None:
    # Fetch the actual pinned core when network is available. The checked-in
    # excerpt keeps local review deterministic when GitHub is unavailable.
    try:
        with urllib.request.urlopen(MESHCORE_URL, timeout=20) as response:
            core = response.read().decode("utf-8")
    except Exception:
        core = pathlib.Path(__file__).with_name("meshcore_fixture.cpp").read_text(
            encoding="utf-8"
        )
    require(core, "case PAYLOAD_TYPE_RAW_CUSTOM")
    require(core, "pkt->isRouteDirect()")
    require(core, "onRawDataRecv(pkt)")
    raw_case = core.split("case PAYLOAD_TYPE_RAW_CUSTOM", 1)[1]
    raw_case = raw_case.split("case PAYLOAD_TYPE_MULTIPART", 1)[0]
    if "sendFlood" in raw_case:
        raise AssertionError("RAW_CUSTOM dispatch must not flood")

    with tempfile.TemporaryDirectory(prefix="v30-transport-") as td:
        exe = pathlib.Path(td) / "transport"
        subprocess.run(
            ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
             str(TEST_CPP), "-o", str(exe)],
            cwd=REPO,
            check=True,
        )
        subprocess.run([str(exe)], cwd=REPO, check=True)

    # This remains the intentional RED assertion until the adapter exists.
    if not (REPO / "v30" / "apply_v30.py").exists():
        raise AssertionError("RED: V30 overlay adapter is not installed yet")
    print("F1 transport corpus OK")


if __name__ == "__main__":
    main()
