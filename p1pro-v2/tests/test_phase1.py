#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(msg: str) -> None:
    raise SystemExit("P1 Pro V2 phase-1 contract failure: " + msg)

def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_phase1.py <V2-patched MeshCore checkout>")
    root = pathlib.Path(sys.argv[1]).resolve()

    variant=(root/"variants/sensecap_solar/platformio.ini").read_text()
    mesh_h=(root/"examples/simple_repeater/MyMesh.h").read_text()
    mesh_cpp=(root/"examples/simple_repeater/MyMesh.cpp").read_text()
    main=(root/"examples/simple_repeater/main.cpp").read_text()
    cli=(root/"src/helpers/CommonCLI.cpp").read_text()
    ctrl=(root/"examples/simple_repeater/AdaptiveMeshController.cpp").read_text()

    required_variant=(
        "MESH_OFFGRIDNL_P1PRO_V2=1",
        "MESH_OFFGRIDNL_P1PRO_EU868_LOCK=1",
        "-D LORA_FREQ=869.618",
        "-D LORA_BW=62.5",
        "-D LORA_SF=8",
        "-D LORA_CR=5",
        "-D LORA_TX_POWER=22",
    )
    for marker in required_variant:
        if marker not in variant: die("missing build contract: "+marker)

    for marker in (
        "_prefs.freq = 869.618f",
        "_prefs.bw = 62.5f",
        "_prefs.sf = 8",
        "_prefs.cr = 5",
        "_prefs.airtime_factor < 9.0f",
        "if (_prefs.tx_power_dbm > 22)",
    ):
        if marker not in mesh_cpp: die("persisted EU868 lock missing: "+marker)

    for marker in (
        "V2 is locked to 869.618/62.5/SF8/CR5",
        "V2 TX range is -9..22 dBm",
        "V2 frequency is locked to 869.618 MHz",
    ):
        if marker not in cli: die("CLI guard missing: "+marker)

    for marker in (
        "v2_adaptive_airtime_factor = 9.0f",
        "_prefs.airtime_factor > v2_adaptive_airtime_factor",
        "_prefs.cad_enabled || v2_adaptive_cad",
        "setV2AdaptiveMeshPolicy",
    ):
        if marker not in mesh_h: die("adaptive Mesh API missing: "+marker)

    for marker in (
        "NORMAL=10%, BUSY=5%, CONGESTED=2%, SEVERE=1%",
        "factors[] = {9.0f, 19.0f, 49.0f, 99.0f}",
        "qmax >= 24",
        "qmax >= 16",
        "qmax >= 8",
        "new_drop",
        "V2_RECOVERY_QUIET_SAMPLES 6",
    ):
        if marker not in ctrl: die("adaptive policy missing: "+marker)

    if "v2_adaptive_mesh.loop();" not in main:
        die("adaptive controller not serviced")

    # V1 guarantees must remain inherited.
    for marker in ("p1_base_station_pool(32)", "baseStationDroppedTx", "baseStationDroppedRx"):
        if marker not in mesh_cpp + "\n" + mesh_h:
            die("V1 resilience inheritance missing: "+marker)

    print("P1 Pro V2 phase-1 contracts OK")


if __name__=="__main__":
    main()
