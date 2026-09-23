#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import shutil
import sys


def fail(message: str) -> None:
    raise SystemExit("P1 Pro V1 phase-2 patch failed: " + message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_phase2.py <phase1-patched MeshCore checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    here = pathlib.Path(__file__).resolve().parent

    required = [
        root / "src/helpers/StaticPoolPacketManager.h",
        root / "src/helpers/StaticPoolPacketManager.cpp",
        root / "src/helpers/CommonCLI.cpp",
        root / "examples/simple_repeater/MyMesh.h",
        root / "examples/simple_repeater/MyMesh.cpp",
        root / "examples/simple_repeater/main.cpp",
    ]
    for p in required:
        if not p.exists():
            fail("missing expected MeshCore file: " + str(p))

    # Copy additive supervisor sources.
    overlay_dir = here / "phase2-overlay"
    for src in overlay_dir.rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(overlay_dir)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    # ------------------------------------------------------------------
    # 1) Instrument the existing bounded packet manager.
    # ------------------------------------------------------------------
    p = root / "src/helpers/StaticPoolPacketManager.h"
    s = p.read_text()
    s = replace_once(
        s,
        "class StaticPoolPacketManager : public mesh::PacketManager {\n  PacketQueue unused, send_queue, rx_queue;\n\npublic:\n",
        """class StaticPoolPacketManager : public mesh::PacketManager {
  PacketQueue unused, send_queue, rx_queue;
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  uint32_t dropped_outbound = 0;
  uint32_t dropped_inbound = 0;
  uint16_t peak_send_queue = 0;
  uint16_t peak_rx_queue = 0;
#endif

public:
""",
        "packet manager counters",
    )
    s = replace_once(
        s,
        "  void queueInbound(mesh::Packet* packet, uint32_t scheduled_for) override;\n  mesh::Packet* getNextInbound(uint32_t now) override;\n};\n",
        """  void queueInbound(mesh::Packet* packet, uint32_t scheduled_for) override;
  mesh::Packet* getNextInbound(uint32_t now) override;
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  int getInboundTotal() const { return rx_queue.count(); }
  uint32_t getDroppedOutbound() const { return dropped_outbound; }
  uint32_t getDroppedInbound() const { return dropped_inbound; }
  uint16_t getPeakSendQueue() const { return peak_send_queue; }
  uint16_t getPeakRxQueue() const { return peak_rx_queue; }
#endif
};
""",
        "packet manager diagnostics getters",
    )
    p.write_text(s)

    p = root / "src/helpers/StaticPoolPacketManager.cpp"
    s = p.read_text()
    s = replace_once(
        s,
        """void StaticPoolPacketManager::queueOutbound(mesh::Packet* packet, uint8_t priority, uint32_t scheduled_for) {
  if (!send_queue.add(packet, priority, scheduled_for)) {
    MESH_DEBUG_PRINTLN("queueOutbound: send queue full, dropping packet");
    free(packet);
  }
}
""",
        """void StaticPoolPacketManager::queueOutbound(mesh::Packet* packet, uint8_t priority, uint32_t scheduled_for) {
  if (!send_queue.add(packet, priority, scheduled_for)) {
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
    dropped_outbound++;
#endif
    MESH_DEBUG_PRINTLN("queueOutbound: send queue full, dropping packet");
    free(packet);
    return;
  }
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  if (send_queue.count() > peak_send_queue) peak_send_queue = send_queue.count();
#endif
}
""",
        "outbound queue instrumentation",
    )
    s = replace_once(
        s,
        """void StaticPoolPacketManager::queueInbound(mesh::Packet* packet, uint32_t scheduled_for) {
  if (!rx_queue.add(packet, 0, scheduled_for)) {
    MESH_DEBUG_PRINTLN("queueInbound: rx queue full, dropping packet");
    free(packet);
  }
}
""",
        """void StaticPoolPacketManager::queueInbound(mesh::Packet* packet, uint32_t scheduled_for) {
  if (!rx_queue.add(packet, 0, scheduled_for)) {
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
    dropped_inbound++;
#endif
    MESH_DEBUG_PRINTLN("queueInbound: rx queue full, dropping packet");
    free(packet);
    return;
  }
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  if (rx_queue.count() > peak_rx_queue) peak_rx_queue = rx_queue.count();
#endif
}
""",
        "inbound queue instrumentation",
    )
    p.write_text(s)

    # ------------------------------------------------------------------
    # 2) Atomic-ish prefs save with backup/rollback on nRF52 P1.
    # ------------------------------------------------------------------
    p = root / "src/helpers/CommonCLI.cpp"
    s = p.read_text()

    s = replace_once(
        s,
        """void CommonCLI::loadPrefs(FILESYSTEM* fs) {
  if (fs->exists("/prefs.json")) {
""",
        """void CommonCLI::loadPrefs(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) && defined(MESH_OFFGRIDNL_P1PRO_V1)
  // If power was lost between backup and replacement, recover the last
  // complete preferences file before attempting to parse.
  if (!fs->exists("/prefs.json") && fs->exists("/prefs.bak")) {
    fs->rename("/prefs.bak", "/prefs.json");
  }
#endif
  if (fs->exists("/prefs.json")) {
""",
        "prefs boot recovery",
    )

    s = replace_once(
        s,
        """    if (file) {
      _prefs->loadSerial(file);   // new Serial prefs
      file.close();
    }
""",
        """    if (file) {
#if defined(NRF52_PLATFORM) && defined(MESH_OFFGRIDNL_P1PRO_V1)
      bool prefs_ok = _prefs->loadSerial(file);
      file.close();
      if (!prefs_ok && fs->exists("/prefs.bak")) {
        File backup = fs->open("/prefs.bak");
        if (backup) {
          _prefs->loadSerial(backup);
          backup.close();
        }
      }
#else
      _prefs->loadSerial(file);   // new Serial prefs
      file.close();
#endif
    }
""",
        "prefs parse fallback",
    )

    old_save = """bool CommonCLI::savePrefs(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove("/prefs.json");
  File file = fs->open("/prefs.json", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open("/prefs.json", "w");
#else
  File file = fs->open("/prefs.json", "w", true);
#endif
  if (file) {
    bool success = _prefs->saveSerial(file);
    file.close();
    return success;
  }
  return false;
}
"""
    new_save = """bool CommonCLI::savePrefs(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) && defined(MESH_OFFGRIDNL_P1PRO_V1)
  // Never destroy the only known-good config before the replacement exists.
  const char* live = "/prefs.json";
  const char* temp = "/prefs.tmp";
  const char* backup = "/prefs.bak";

  fs->remove(temp);
  File file = fs->open(temp, FILE_O_WRITE);
  if (!file) return false;

  bool success = _prefs->saveSerial(file);
  file.close();
  if (!success) {
    fs->remove(temp);
    return false;
  }

  fs->remove(backup);
  if (fs->exists(live) && !fs->rename(live, backup)) {
    fs->remove(temp);
    return false;
  }

  if (!fs->rename(temp, live)) {
    if (fs->exists(backup) && !fs->exists(live)) {
      fs->rename(backup, live);
    }
    fs->remove(temp);
    return false;
  }

  // The live file is complete; the backup is no longer needed.
  fs->remove(backup);
  return true;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove("/prefs.json");
  File file = fs->open("/prefs.json", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open("/prefs.json", "w");
#else
  File file = fs->open("/prefs.json", "w", true);
#endif
#if !(defined(NRF52_PLATFORM) && defined(MESH_OFFGRIDNL_P1PRO_V1))
  if (file) {
    bool success = _prefs->saveSerial(file);
    file.close();
    return success;
  }
  return false;
#endif
}
"""
    s = replace_once(s, old_save, new_save, "safe preferences save")
    p.write_text(s)

    # ------------------------------------------------------------------
    # 3) Give the P1 build a named pool so health can be observed.
    # ------------------------------------------------------------------
    p = root / "examples/simple_repeater/MyMesh.cpp"
    s = p.read_text()
    s = replace_once(
        s,
        '#include "MyMesh.h"\n',
        '#include "MyMesh.h"\n'
        '#if defined(MESH_OFFGRIDNL_P1PRO_V1)\n'
        'static StaticPoolPacketManager p1_base_station_pool(32);\n'
        '#endif\n',
        "named P1 packet pool",
    )

    old_ctor = """: mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(32), tables),
      region_map(key_store), temp_map(key_store),
"""
    new_ctor = """#if defined(MESH_OFFGRIDNL_P1PRO_V1)
    : mesh::Mesh(radio, ms, rng, rtc, p1_base_station_pool, tables),
#else
    : mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(32), tables),
#endif
      region_map(key_store), temp_map(key_store),
"""
    s = replace_once(s, old_ctor, new_ctor, "P1 packet pool constructor")

    # Fresh P1 installs sleep the nRF52 when there is no pending mesh work.
    s = replace_once(
        s,
        """  _prefs.gps_enabled = 0;
  _prefs.gps_interval = 0;
  _prefs.advert_loc_policy = ADVERT_LOC_PREFS;
""",
        """  _prefs.gps_enabled = 0;
  _prefs.gps_interval = 0;
  _prefs.advert_loc_policy = ADVERT_LOC_PREFS;
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  _prefs.powersaving_enabled = 1;
#endif
""",
        "P1 power-saving default",
    )

    # Periodic adverts are background work. Under queue pressure, reschedule them
    # without allocating another packet. Manual adverts still remain available.
    s = replace_once(
        s,
        "    mesh::Packet *pkt = createSelfAdvert();\n    uint32_t delay_millis = 0;\n",
        """#if defined(MESH_OFFGRIDNL_P1PRO_V1)
    mesh::Packet *pkt = baseStationCongested() ? nullptr : createSelfAdvert();
#else
    mesh::Packet *pkt = createSelfAdvert();
#endif
    uint32_t delay_millis = 0;
""",
        "flood advert congestion suppression",
    )
    s = replace_once(
        s,
        "    mesh::Packet *pkt = createSelfAdvert();\n    if (pkt) sendZeroHop(pkt);\n",
        """#if defined(MESH_OFFGRIDNL_P1PRO_V1)
    mesh::Packet *pkt = baseStationCongested() ? nullptr : createSelfAdvert();
#else
    mesh::Packet *pkt = createSelfAdvert();
#endif
    if (pkt) sendZeroHop(pkt);
""",
        "local advert congestion suppression",
    )

    # Add concrete health accessors and compact CLI formatter.
    insert_at = s.find("void MyMesh::formatStatsReply(char *reply) {")
    if insert_at < 0:
        fail("formatStatsReply anchor missing")
    health_impl = r'''#if defined(MESH_OFFGRIDNL_P1PRO_V1)
int MyMesh::baseStationFreePackets() const { return p1_base_station_pool.getFreeCount(); }
int MyMesh::baseStationTxQueued() const { return p1_base_station_pool.getOutboundTotal(); }
int MyMesh::baseStationRxQueued() const { return p1_base_station_pool.getInboundTotal(); }
uint32_t MyMesh::baseStationDroppedTx() const { return p1_base_station_pool.getDroppedOutbound(); }
uint32_t MyMesh::baseStationDroppedRx() const { return p1_base_station_pool.getDroppedInbound(); }
uint16_t MyMesh::baseStationPeakTx() const { return p1_base_station_pool.getPeakSendQueue(); }
uint16_t MyMesh::baseStationPeakRx() const { return p1_base_station_pool.getPeakRxQueue(); }

bool MyMesh::baseStationCongested() const {
  return p1_base_station_pool.getFreeCount() <= 6 ||
         p1_base_station_pool.getOutboundTotal() >= 24 ||
         p1_base_station_pool.getInboundTotal() >= 24;
}

void MyMesh::formatBaseStationHealth(char* reply) const {
  snprintf(reply, 160,
           "P1V1 free:%d tx:%d rx:%d drop:%lu/%lu peak:%u/%u air:%lu",
           baseStationFreePackets(), baseStationTxQueued(), baseStationRxQueued(),
           (unsigned long)baseStationDroppedTx(), (unsigned long)baseStationDroppedRx(),
           (unsigned)baseStationPeakTx(), (unsigned)baseStationPeakRx(),
           (unsigned long)getTotalAirTime());
}
#endif

'''
    s = s[:insert_at] + health_impl + s[insert_at:]

    # Local/authorized management health command.
    anchor = """  } else{
    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }
}
"""
    replacement = """#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  } else if (strcmp(command, "base health") == 0) {
    formatBaseStationHealth(reply);
#endif
  } else{
    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }
}
"""
    s = replace_once(s, anchor, replacement, "base health command")
    p.write_text(s)

    p = root / "examples/simple_repeater/MyMesh.h"
    s = p.read_text()
    anchor = """  bool setRxBoostedGain(bool enable) override;

  #if defined(USE_LR2021)
"""
    replacement = """  bool setRxBoostedGain(bool enable) override;

#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  int baseStationFreePackets() const;
  int baseStationTxQueued() const;
  int baseStationRxQueued() const;
  uint32_t baseStationDroppedTx() const;
  uint32_t baseStationDroppedRx() const;
  uint16_t baseStationPeakTx() const;
  uint16_t baseStationPeakRx() const;
  bool baseStationCongested() const;
  void formatBaseStationHealth(char* reply) const;
#endif

  #if defined(USE_LR2021)
"""
    s = replace_once(s, anchor, replacement, "base station health declarations")
    p.write_text(s)

    # ------------------------------------------------------------------
    # 4) Integrate the supervisor into the normal repeater main loop.
    # ------------------------------------------------------------------
    p = root / "examples/simple_repeater/main.cpp"
    s = p.read_text()
    s = replace_once(
        s,
        '#include "MyMesh.h"\n',
        '#include "MyMesh.h"\n'
        '#if defined(MESH_OFFGRIDNL_P1PRO_V1)\n'
        '#include "BaseStationSupervisor.h"\n'
        '#endif\n',
        "supervisor include",
    )
    s = replace_once(
        s,
        "MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);\n",
        """MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
static BaseStationSupervisor base_station_supervisor(board, the_mesh);
#endif
""",
        "supervisor instance",
    )
    s = replace_once(
        s,
        "  board.onBootComplete();\n}\n",
        """  board.onBootComplete();
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  base_station_supervisor.begin();
#endif
}
""",
        "supervisor begin",
    )
    s = replace_once(
        s,
        "  the_mesh.loop();\n  sensors.loop();\n",
        """  the_mesh.loop();
#if defined(MESH_OFFGRIDNL_P1PRO_V1)
  base_station_supervisor.loop();
#endif
  sensors.loop();
""",
        "supervisor loop",
    )
    p.write_text(s)

    # Fast pre-build assertions.
    checks = {
        "src/helpers/StaticPoolPacketManager.cpp": [
            "dropped_outbound++", "dropped_inbound++",
            "peak_send_queue", "peak_rx_queue",
        ],
        "src/helpers/CommonCLI.cpp": [
            '"/prefs.tmp"', '"/prefs.bak"',
            'fs->rename(temp, live)', 'fs->rename(backup, live)',
        ],
        "examples/simple_repeater/MyMesh.cpp": [
            "p1_base_station_pool(32)", "baseStationCongested() ? nullptr",
            "formatBaseStationHealth", "_prefs.powersaving_enabled = 1",
        ],
        "examples/simple_repeater/main.cpp": [
            "BaseStationSupervisor", "base_station_supervisor.loop()",
        ],
        "examples/simple_repeater/BaseStationSupervisor.cpp": [
            "P1_POOL_STALL_REBOOT_MS", "free_packets == 0",
        ],
    }
    for rel, markers in checks.items():
        data = (root / rel).read_text()
        for marker in markers:
            if marker not in data:
                fail(f"{rel}: missing marker {marker}")

    print("P1 Pro V1 phase-2 resilience overlay applied")


if __name__ == "__main__":
    main()
