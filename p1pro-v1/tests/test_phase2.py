#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def die(message: str) -> None:
    raise SystemExit("P1 Pro V1 phase-2 contract failure: " + message)


def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_phase2.py <phase2-patched MeshCore checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    mgr_h = (root / "src/helpers/StaticPoolPacketManager.h").read_text()
    mgr_cpp = (root / "src/helpers/StaticPoolPacketManager.cpp").read_text()
    cli = (root / "src/helpers/CommonCLI.cpp").read_text()
    mesh_h = (root / "examples/simple_repeater/MyMesh.h").read_text()
    mesh_cpp = (root / "examples/simple_repeater/MyMesh.cpp").read_text()
    main_cpp = (root / "examples/simple_repeater/main.cpp").read_text()
    supervisor = (root / "examples/simple_repeater/BaseStationSupervisor.cpp").read_text()

    # Memory remains bounded at the proven phase-1 pool size.
    if "p1_base_station_pool(32)" not in mesh_cpp:
        die("P1 pool is not fixed at 32 packets")
    if "StaticPoolPacketManager(64)" in mesh_cpp:
        die("packet pool was silently enlarged")

    # Queue pressure must be observable and drops must remain controlled.
    for marker in (
        "dropped_outbound", "dropped_inbound",
        "peak_send_queue", "peak_rx_queue",
        "getInboundTotal()",
    ):
        if marker not in mgr_h + "\n" + mgr_cpp:
            die("queue telemetry missing: " + marker)

    # Background adverts are suppressed before allocating a packet under pressure.
    if mesh_cpp.count("baseStationCongested() ? nullptr : createSelfAdvert()") != 2:
        die("periodic advert congestion suppression missing")

    # Safe preference replacement must preserve a recoverable prior config.
    for marker in (
        '"/prefs.tmp"', '"/prefs.bak"',
        "fs->rename(live, backup)",
        "fs->rename(temp, live)",
        "fs->rename(backup, live)",
    ):
        if marker not in cli:
            die("safe prefs transaction missing: " + marker)

    # Supervisor recovery is intentionally conservative.
    for marker in (
        "#define P1_POOL_STALL_REBOOT_MS 120000UL",
        "free_packets == 0",
        "pool_stalled && no_progress",
        "board.reboot()",
    ):
        if marker not in supervisor:
            die("stall supervisor missing: " + marker)

    # nRF power saving must be the fresh-install default, without forcing GPS on.
    if "_prefs.powersaving_enabled = 1;" not in mesh_cpp:
        die("P1 power-saving default missing")
    if "_prefs.gps_enabled = 1;" in mesh_cpp:
        die("phase 2 must not force continuous GPS")

    # Diagnostics are available without changing the protocol format.
    for marker in (
        "baseStationFreePackets", "baseStationDroppedTx",
        "formatBaseStationHealth", '"base health"',
    ):
        if marker not in mesh_h + "\n" + mesh_cpp:
            die("base-station diagnostics missing: " + marker)

    # Supervisor must be integrated in the standard loop.
    if "base_station_supervisor.loop();" not in main_cpp:
        die("supervisor is not serviced")

    print("P1 Pro V1 phase-2 resilience contracts OK")


if __name__ == "__main__":
    main()
