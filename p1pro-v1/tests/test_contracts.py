#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

def die(message: str) -> None:
    raise SystemExit("P1 Pro V1 contract failure: " + message)

def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_contracts.py <patched MeshCore checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    variant = (root / "variants/sensecap_solar/platformio.ini").read_text()
    repeater = (root / "examples/simple_repeater/MyMesh.cpp").read_text()
    meshcore = (root / "src/MeshCore.h").read_text()
    routing = (root / "src/helpers/RoutingPolicy.h").read_text()

    # Exact target/hardware contract.
    for marker in (
        "[env:SenseCap_Solar_repeater]",
        "-D NRF52_PLATFORM=1",
        "-D NRF52_POWER_MANAGEMENT",
        "-D USE_SX1262",
        "-D SX126X_DIO2_AS_RF_SWITCH=1",
        "-D SX126X_DIO3_TCXO_VOLTAGE=1.8",
        "-D LORA_TX_POWER=22",
        "MESH_OFFGRIDNL_P1PRO_V1=1",
    ):
        if marker not in variant:
            die("hardware/build marker missing: " + marker)

    # 48-hop design requires one-byte path support.
    if "#define PATH_HASH_SIZE       1" not in meshcore:
        die("MeshCore one-byte path hash contract changed")

    for marker in (
        "MESH_OFFGRIDNL_P1PRO_FLOOD_MAX=48",
        "MESH_OFFGRIDNL_P1PRO_UNSCOPED_MAX=6",
        "MESH_OFFGRIDNL_P1PRO_ADVERT_MAX=8",
        "-D LORA_FREQ=869.618",
        "-D LORA_BW=62.5",
        "-D LORA_SF=8",
        "-D LORA_CR=5",
        "-D LORA_TX_POWER=22",
        "_prefs.path_hash_mode = 0;",
    ):
        if marker not in variant + "\n" + repeater:
            die("routing profile missing: " + marker)

    # Hop enforcement must remain upstream and apply before forwarding.
    for marker in (
        "hops >= flood_max",
        "hops >= flood_max_unscoped",
        "hops >= flood_max_advert",
    ):
        if marker not in routing:
            die("routing hop-limit enforcement missing: " + marker)

    # No memory inflation in phase 1.
    if "StaticPoolPacketManager(32)" not in repeater:
        die("static packet pool changed; memory budget requires a separate measured review")

    # Preserve upstream ingress rate limiters.
    for marker in ("discover_limiter(4, 120)", "anon_limiter(4, 180)"):
        if marker not in repeater:
            die("rate limiter missing: " + marker)

    # Persisted values are capped after load.
    load = repeater.find("_cli.loadPrefs(_fs);")
    acl = repeater.find("acl.load(_fs, self_id);", load)
    if load < 0 or acl < 0:
        die("prefs load boundary missing")
    block = repeater[load:acl]
    for marker in (
        "_prefs.flood_max > MESH_OFFGRIDNL_P1PRO_FLOOD_MAX",
        "_prefs.flood_max_unscoped > MESH_OFFGRIDNL_P1PRO_UNSCOPED_MAX",
        "_prefs.flood_max_advert > MESH_OFFGRIDNL_P1PRO_ADVERT_MAX",
        "_prefs.path_hash_mode = 0;",
    ):
        if marker not in block:
            die("persisted-pref cap missing: " + marker)

    # Phase-1 intentionally does not introduce unbounded dynamic packet containers.
    forbidden = (
        "std::vector<mesh::Packet",
        "std::list<mesh::Packet",
        "while (true) { send",
    )
    for bad in forbidden:
        if bad in repeater:
            die("forbidden unbounded pattern found: " + bad)

    print("P1 Pro V1 contracts OK")

if __name__ == "__main__":
    main()
