#!/usr/bin/env python3
"""F1 packet-routing corpus tied to immutable MeshCore and V30 adapter."""
from __future__ import annotations
import pathlib
import subprocess
import tempfile
import urllib.request
REPO = pathlib.Path(__file__).resolve().parents[2]
TEST_CPP = pathlib.Path(__file__).with_name("test_transport.cpp")
ADAPTER_CPP = REPO / "v30/overlay/src/helpers/esp32/V30EmergencyTransport.cpp"
MESHCORE_SHA = "edd7ed47f42acf6c64c9fcac551050fdbd531a09"
MESHCORE_URL = "https://raw.githubusercontent.com/ALLFATHER-BV/meshcomod/" + MESHCORE_SHA + "/src/Mesh.cpp"
def require(text: str, marker: str) -> None:
    if marker not in text: raise AssertionError(f"actual-core fixture missing {marker!r}")
def main() -> None:
    try:
        with urllib.request.urlopen(MESHCORE_URL, timeout=20) as response:
            core = response.read().decode("utf-8")
    except Exception:
        core = pathlib.Path(__file__).with_name("meshcore_fixture.cpp").read_text(encoding="utf-8")
    require(core, "case PAYLOAD_TYPE_RAW_CUSTOM")
    require(core, "pkt->isRouteDirect()")
    require(core, "onRawDataRecv(pkt)")
    raw_case = core.split("case PAYLOAD_TYPE_RAW_CUSTOM", 1)[1].split("case PAYLOAD_TYPE_MULTIPART", 1)[0]
    if "sendFlood" in raw_case: raise AssertionError("RAW_CUSTOM dispatch must not flood")
    with tempfile.TemporaryDirectory(prefix="v30-transport-") as td:
        exe = pathlib.Path(td) / "transport"
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ADAPTER_CPP.parent), str(TEST_CPP), str(ADAPTER_CPP), "-o", str(exe)], cwd=REPO, check=True)
        subprocess.run([str(exe)], cwd=REPO, check=True)
    apply = (REPO / "v30" / "apply_v30.py").read_text(encoding="utf-8")
    for marker in ("WADAMESH_SHA", "MESHCORE_SHA", "v30TryEnqueueEmergencyRaw", "sendZeroHop", "onV30TxResult"):
        require(apply, marker)
    print("F1 transport corpus OK")
if __name__ == "__main__": main()
