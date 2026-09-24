#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import sys


def die(message: str) -> None:
    raise SystemExit("P1 Pro V8 V19 BLE contract failure: " + message)


def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_contracts.py <patched MeshCore checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio = (root / "variants/sensecap_solar/platformio.ini").read_text()
    mesh = (root / "examples/companion_radio/MyMesh.cpp").read_text()
    mesh_h = (root / "examples/companion_radio/MyMesh.h").read_text()

    profile = json.loads((pathlib.Path(__file__).resolve().parents[1] / "profile.json").read_text())

    # Build must remain the BLE companion target, not the repeater target.
    ble_start = pio.find("[env:SenseCap_Solar_companion_radio_ble]")
    usb_start = pio.find("[env:SenseCap_Solar_companion_radio_usb]", ble_start)
    repeater_start = pio.find("[env:SenseCap_Solar_repeater]")
    room_start = pio.find("[env:SenseCap_Solar_room_server]", repeater_start)
    if min(ble_start, usb_start, repeater_start, room_start) < 0:
        die("SenseCAP target sections missing")

    ble = pio[ble_start:usb_start]
    repeater = pio[repeater_start:room_start]

    required_ble = (
        "MESH_OFFGRIDNL_P1PRO_V8_V19=1",
        "BLE_PIN_CODE=123456",
        "+<helpers/nrf52/SerialBLEInterface.cpp>",
        "+<../examples/companion_radio/*.cpp>",
    )
    for marker in required_ble:
        if marker not in ble:
            die("BLE target missing " + marker)

    if "MESH_OFFGRIDNL_P1PRO_V8_V19=1" in repeater:
        die("V8 V19 flag leaked into repeater target")

    # Exact RF profile and explicit app-side lock.
    required_mesh = (
        "V8_V19_FREQ_MHZ = 869.618f",
        "V8_V19_BW_KHZ = 62.5f",
        "V8_V19_FREQ_KHZ = 869618",
        "V8_V19_BW_HZ = 62500",
        "V8_V19_SF = 8",
        "V8_V19_CR = 5",
        "V8_V19_TX_MAX_DBM = 22",
        "_prefs.freq = V8_V19_FREQ_MHZ;",
        "_prefs.bw = V8_V19_BW_KHZ;",
        "_prefs.sf = V8_V19_SF;",
        "_prefs.cr = V8_V19_CR;",
        "const bool v8_v19_profile",
        "freq == V8_V19_FREQ_KHZ",
        "bw == V8_V19_BW_HZ",
        "sf == V8_V19_SF",
        "cr == V8_V19_CR",
        "power > V8_V19_TX_MAX_DBM",
        "V8 V19 lock rejected radio params",
        "radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);",
    )
    for marker in required_mesh:
        if marker not in mesh:
            die("patched companion missing " + marker)

    # Persisted settings must be repaired before the driver receives them.
    load = mesh.find("_store->loadPrefs(_prefs);")
    lock = mesh.find("const bool v8_v19_radio_changed", load)
    apply_driver = mesh.find("radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);", lock)
    if not (0 <= load < lock < apply_driver):
        die("persisted V19 lock ordering is wrong")

    # Do not broaden upstream repeat frequencies for this direct companion profile.
    if "ALLOWED_REPEAT_FREQ_RANGE" in ble:
        die("V8 must not override allowed repeat frequency ranges")
    for marker in ("{ 433000, 433000 }", "{ 869495, 869495 }", "{ 918000, 918000 }"):
        if marker not in mesh:
            die("upstream repeat-range marker missing: " + marker)
    if "{ 869618, 869618 }" in mesh:
        die("869.618 must not be silently added as a client-repeat frequency")

    # Keep the official companion feature protocol/version identity.
    if "#define FIRMWARE_VER_CODE 13" not in mesh_h:
        die("unexpected companion feature protocol code")
    if '#define FIRMWARE_VERSION "v1.17.1"' not in mesh_h:
        die("unexpected MeshCore companion version")
    if '#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="' not in mesh:
        die("public channel PSK changed")

    assert profile["upstream"]["commit"] == "d92964352441e53b93e8667b802e04f6e072b39e"
    assert profile["upstream"]["target"] == "SenseCap_Solar_companion_radio_ble"
    assert profile["partner"]["wadameshCommit"] == "6fe6b9f06332708b6ed0cc78958017d0dcc9e35d"
    assert profile["radioProfile"] == {
        "band": "EU868",
        "frequencyMHz": 869.618,
        "bandwidthKHz": 62.5,
        "spreadingFactor": 8,
        "codingRate": 5,
        "txPowerDefaultDbm": 22,
        "txPowerMaxDbm": 22,
        "locked": True,
    }
    assert profile["companion"]["bluetooth"] is True
    assert profile["companion"]["role"] == "companion_radio_ble"
    assert profile["validation"]["stable"] is False

    print("P1 Pro V8 V19 BLE contracts OK")


if __name__ == "__main__":
    main()
