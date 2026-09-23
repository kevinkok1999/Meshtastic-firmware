#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(msg: str) -> None:
    raise SystemExit("P1 Pro V2 phase-2 contract failure: " + msg)

def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_phase2.py <V2-phase2-patched MeshCore checkout>")

    root=pathlib.Path(sys.argv[1]).resolve()
    board=(root/"variants/sensecap_solar/SenseCapSolarBoard.h").read_text()
    mgr_h=(root/"src/helpers/StaticPoolPacketManager.h").read_text()
    mgr_cpp=(root/"src/helpers/StaticPoolPacketManager.cpp").read_text()
    mesh_h=(root/"examples/simple_repeater/MyMesh.h").read_text()
    mesh_cpp=(root/"examples/simple_repeater/MyMesh.cpp").read_text()
    supervisor=(root/"examples/simple_repeater/BaseStationSupervisor.cpp").read_text()
    solar=(root/"examples/simple_repeater/SolarGuardian.cpp").read_text()
    main=(root/"examples/simple_repeater/main.cpp").read_text()

    for marker in (
        "enterV2LowVoltageProtection",
        "SHUTDOWN_REASON_LOW_VOLTAGE",
        "shutdownPeripherals()",
    ):
        if marker not in board: die("protected low-voltage path missing: "+marker)

    for marker in (
        "recoverQueues()",
        "send_queue.removeByIdx(0)",
        "rx_queue.removeByIdx(0)",
    ):
        if marker not in mgr_h+"\n"+mgr_cpp:
            die("bounded queue recovery missing: "+marker)

    for marker in (
        "v2_power_airtime_factor",
        "v2_background_suppressed",
        "setV2PowerPolicy",
        "v2RecoverPacketQueues",
        "v2RecoverRadio",
    ):
        if marker not in mesh_h+"\n"+mesh_cpp:
            die("V2 power/recovery API missing: "+marker)

    for marker in (
        'strcmp(_prefs.password, "password") == 0',
        "random_secret[7]",
    ):
        if marker not in mesh_cpp:
            die("unique credential generation missing: "+marker)

    for marker in (
        "formatV2LocalCredential",
        'strcmp(command, "base credential") == 0',
    ):
        if marker not in mesh_h+"\n"+main:
            die("USB-only credential route missing: "+marker)

    if 'strcmp(command, "base credential") == 0' in mesh_cpp:
        die("credential command must not be reachable through MyMesh remote command path")

    for marker in (
        "#define V2_RECOVERY_STAGE_MS 60000UL",
        "v2RecoverPacketQueues()",
        "v2RecoverRadio()",
        "staged recovery exhausted",
        "board.reboot()",
    ):
        if marker not in supervisor:
            die("staged recovery missing: "+marker)

    for marker in (
        "#define V2_POWER_ECO_MV           3700",
        "#define V2_POWER_SURVIVAL_MV      3500",
        "#define V2_POWER_CRITICAL_MV      3350",
        "#define V2_POWER_BOOTLOCK_MV      3300",
        "V2_POWER_SHUTDOWN_SAMPLES 3",
        "factors[] = {9.0f, 19.0f, 49.0f, 99.0f}",
        "state >= SURVIVAL",
        "enterV2LowVoltageProtection",
    ):
        if marker not in solar:
            die("Solar Guardian contract missing: "+marker)

    if "v2_solar_guardian.loop();" not in main:
        die("Solar Guardian not serviced")

    print("P1 Pro V2 phase-2 contracts OK")

if __name__=="__main__":
    main()
