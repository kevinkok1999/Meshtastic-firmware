#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def die(message: str) -> None:
    raise SystemExit("V26 contract failure: " + message)


def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_contracts.py <patched checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio = (root / "platformio.ini").read_text()
    main_src = (root / "src/main.cpp").read_text()
    mesh_h = (root / "src/MyMesh.h").read_text()
    mesh_cpp = (root / "src/MyMesh.cpp").read_text()

    # V26 must sit on the proven V19 stack, not replace it.
    for flag in (
        "MESH_OFFGRIDNL_V11=1",
        "MESH_OFFGRIDNL_V12=1",
        "MESH_OFFGRIDNL_V13=1",
        "MESH_OFFGRIDNL_V15=1",
        "MESH_OFFGRIDNL_V16=1",
        "MESH_OFFGRIDNL_V17=1",
        "MESH_OFFGRIDNL_V18=1",
        "MESH_OFFGRIDNL_V19=1",
        "MESH_OFFGRIDNL_V26=1",
    ):
        if flag not in pio:
            die("missing " + flag)

    # V26 must be enabled only in the T-Deck target section.
    start = pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    end = pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", start)
    if start < 0 or end < 0:
        die("T-Deck target section missing")
    tdeck = pio[start:end]
    if "MESH_OFFGRIDNL_V26=1" not in tdeck:
        die("V26 flag not present in T-Deck section")
    if pio.count("MESH_OFFGRIDNL_V26=1") != 1:
        die("V26 flag must be T-Deck-only")

    # Preserve V19's Wi-Fi recovery behavior exactly.
    for marker in (
        'v19_factory.begin("mog_v19", false)',
        "wifiConfigClear();",
        "wifiConfigSetRadioEnabled(true);",
        "wifiConfigSetWifiChosen(true);",
        "SdNvsPrefs::flush(4000)",
        "[V19][wifi] attempt=%u standard join",
        "WiFi.begin(ssid, v19_pwd);",
    ):
        if marker not in main_src:
            die("lost V19 marker " + marker)

    # Preventive recovery is deliberately slow because the current SX126x reset
    # performs a full calibration, not merely a state flip.
    for marker in (
        "int getAGCResetInterval() const override;",
        "return 5 * 60 * 1000;",
    ):
        if marker not in (mesh_h + "\n" + mesh_cpp):
            die("preventive AGC contract missing " + marker)

    # Adaptive guard inputs: decode errors, RX events, queue drops and noise.
    required_main = (
        "V26_RF_EVAL_MS = 15000",
        "V26_RF_BOOT_GRACE_MS = 60000",
        "V26_RF_MIN_ADAPTIVE_RESET_MS = 90000",
        "V26_RF_MISSED_EVENT_THRESHOLD = 4",
        "V26_RF_ERROR_PERMILLE_THRESHOLD = 250",
        "V26_RF_BLOCKER_NOISE_DBM = -98",
        "radio_driver.getPacketsRecv()",
        "radio_driver.getPacketsRecvErrors()",
        "radio_driver.getRxEvents()",
        "radio_driver.getRxQueueDrops()",
        "radio_driver.getNoiseFloor()",
        "const bool consumer_pressure = drop_delta != 0;",
        "!consumer_pressure",
        "radio_driver.isInRecvMode() && !radio_driver.isReceiving()",
        "radio_driver.resetAGC();",
        "radio_driver.triggerNoiseFloorCalibrate(0);",
        "if (!spectrumOwnsRadio()) v26RfMaintenance();",
    )
    for marker in required_main:
        if marker not in main_src:
            die("RF guard missing " + marker)

    # Scope the actual guard block and reject compatibility-breaking behavior.
    s = main_src.find("#if defined(MESH_OFFGRIDNL_V26)\n// MeshOffGridNL V26 RF guard.")
    e = main_src.find("\n#endif\n\nvoid loop()", s)
    if s < 0 or e < 0:
        die("V26 RF guard block missing")
    block = main_src[s:e]

    for forbidden in (
        "setFrequency(",
        "setBandwidth(",
        "setSpreadingFactor(",
        "setCodingRate(",
        "spectralScan",
        "setRxBoostedGainMode(false",
        "WiFi.",
        "delay(",
    ):
        if forbidden in block:
            die("RF guard contains forbidden behavior " + forbidden)

    # No unsolicited release logging on the USB companion binary stream.
    if "Serial." in block:
        die("RF guard must not write release diagnostics to companion Serial")

    print("V26 contracts OK: V19 preserved, RF guard bounded, no modem/channel drift")


if __name__ == "__main__":
    main()
