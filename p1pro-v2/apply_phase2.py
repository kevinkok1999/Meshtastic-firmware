#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import shutil
import sys


def fail(message: str) -> None:
    raise SystemExit("P1 Pro V2 phase-2 patch failed: " + message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_phase2.py <V2-phase1-patched MeshCore checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    here = pathlib.Path(__file__).resolve().parent

    board_h = root / "variants/sensecap_solar/SenseCapSolarBoard.h"
    mgr_h = root / "src/helpers/StaticPoolPacketManager.h"
    mgr_cpp = root / "src/helpers/StaticPoolPacketManager.cpp"
    mesh_h = root / "examples/simple_repeater/MyMesh.h"
    mesh_cpp = root / "examples/simple_repeater/MyMesh.cpp"
    main_cpp = root / "examples/simple_repeater/main.cpp"

    for p in (board_h, mgr_h, mgr_cpp, mesh_h, mesh_cpp, main_cpp):
        if not p.exists():
            fail("missing expected MeshCore file: " + str(p))

    overlay_dir = here / "phase2-overlay"
    for src in overlay_dir.rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(overlay_dir)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    # ------------------------------------------------------------------
    # SenseCAP: expose the existing protected low-voltage shutdown path.
    # It already arms LPCOMP + VBUS wake for SHUTDOWN_REASON_LOW_VOLTAGE.
    # ------------------------------------------------------------------
    s = board_h.read_text()
    s = replace_once(
        s,
        """  SenseCapSolarBoard() : NRF52Board("SENSECAP_SOLAR_OTA") {}
  void begin();

""",
        """  SenseCapSolarBoard() : NRF52Board("SENSECAP_SOLAR_OTA") {}
  void begin();

#if defined(MESH_OFFGRIDNL_P1PRO_V2) && defined(NRF52_POWER_MANAGEMENT)
  void enterV2LowVoltageProtection() {
    shutdownPeripherals();
    initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE);
  }
#endif

""",
        "SenseCAP low-voltage wrapper",
    )
    board_h.write_text(s)

    # ------------------------------------------------------------------
    # Bounded queue soft recovery: drop queued work back into the existing
    # fixed pool before considering a radio reset or MCU reboot.
    # ------------------------------------------------------------------
    s = mgr_h.read_text()
    anchor = """  uint16_t getPeakRxQueue() const { return peak_rx_queue; }
#endif
};
"""
    replacement = """  uint16_t getPeakRxQueue() const { return peak_rx_queue; }
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  int recoverQueues();
#endif
#endif
};
"""
    s = replace_once(s, anchor, replacement, "queue recovery declaration")
    mgr_h.write_text(s)

    s = mgr_cpp.read_text()
    anchor = """mesh::Packet* StaticPoolPacketManager::getNextInbound(uint32_t now) {
  return rx_queue.get(now);
}
"""
    replacement = """mesh::Packet* StaticPoolPacketManager::getNextInbound(uint32_t now) {
  return rx_queue.get(now);
}

#if defined(MESH_OFFGRIDNL_P1PRO_V2)
int StaticPoolPacketManager::recoverQueues() {
  int recovered = 0;
  mesh::Packet* packet = nullptr;
  while ((packet = send_queue.removeByIdx(0)) != nullptr) {
    free(packet);
    recovered++;
  }
  while ((packet = rx_queue.removeByIdx(0)) != nullptr) {
    free(packet);
    recovered++;
  }
  return recovered;
}
#endif
"""
    s = replace_once(s, anchor, replacement, "queue recovery implementation")
    mgr_cpp.write_text(s)

    # ------------------------------------------------------------------
    # MyMesh: combine adaptive-mesh + power budgets, unique credential,
    # and staged recovery hooks.
    # ------------------------------------------------------------------
    s = mesh_h.read_text()
    anchor = """#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  float v2_adaptive_airtime_factor = 9.0f;
  bool v2_adaptive_cad = false;
  uint8_t v2_mesh_pressure_state = 0;
#endif
"""
    replacement = """#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  float v2_adaptive_airtime_factor = 9.0f;
  float v2_power_airtime_factor = 9.0f;
  bool v2_adaptive_cad = false;
  bool v2_background_suppressed = false;
  uint8_t v2_mesh_pressure_state = 0;
  uint8_t v2_power_state = 0;
  uint16_t v2_last_battery_mv = 0;
#endif
"""
    s = replace_once(s, anchor, replacement, "V2 power state members")

    anchor = """  float getAirtimeBudgetFactor() const override {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    return _prefs.airtime_factor > v2_adaptive_airtime_factor
        ? _prefs.airtime_factor : v2_adaptive_airtime_factor;
#else
    return _prefs.airtime_factor;
#endif
  }
"""
    replacement = """  float getAirtimeBudgetFactor() const override {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    float factor = _prefs.airtime_factor;
    if (v2_adaptive_airtime_factor > factor) factor = v2_adaptive_airtime_factor;
    if (v2_power_airtime_factor > factor) factor = v2_power_airtime_factor;
    return factor;
#else
    return _prefs.airtime_factor;
#endif
  }
"""
    s = replace_once(s, anchor, replacement, "combined airtime policy")

    anchor = """  float getV2AdaptiveAirtimeFactor() const { return v2_adaptive_airtime_factor; }
  bool getV2AdaptiveCad() const { return v2_adaptive_cad; }
  uint8_t getV2MeshPressureState() const { return v2_mesh_pressure_state; }
#endif
"""
    replacement = """  float getV2AdaptiveAirtimeFactor() const { return v2_adaptive_airtime_factor; }
  bool getV2AdaptiveCad() const { return v2_adaptive_cad; }
  uint8_t getV2MeshPressureState() const { return v2_mesh_pressure_state; }

  void setV2PowerPolicy(float airtime_factor, bool suppress_background,
                        uint8_t power_state, uint16_t battery_mv) {
    v2_power_airtime_factor = airtime_factor < 9.0f ? 9.0f : airtime_factor;
    v2_background_suppressed = suppress_background;
    v2_power_state = power_state;
    v2_last_battery_mv = battery_mv;
  }
  uint8_t getV2PowerState() const { return v2_power_state; }
  uint16_t getV2BatteryMv() const { return v2_last_battery_mv; }
  bool getV2BackgroundSuppressed() const { return v2_background_suppressed; }
  int v2RecoverPacketQueues();
  bool v2RecoverRadio();
#endif
"""
    s = replace_once(s, anchor, replacement, "V2 power/recovery API")
    mesh_h.write_text(s)

    s = mesh_cpp.read_text()

    # First-boot credential hardening. Preserve an already-custom password.
    anchor = """  if (_prefs.airtime_factor < 9.0f) _prefs.airtime_factor = 9.0f;
#endif
  acl.load(_fs, self_id);
"""
    replacement = """  if (_prefs.airtime_factor < 9.0f) _prefs.airtime_factor = 9.0f;

  // Replace the upstream shared default with a device-local random credential.
  // Existing non-default passwords are preserved across V1 -> V2 upgrades.
  if (_prefs.password[0] == 0 || strcmp(_prefs.password, "password") == 0) {
    uint8_t random_secret[7];
    char generated[15];
    getRNG()->random(random_secret, sizeof(random_secret));
    mesh::Utils::toHex(generated, random_secret, sizeof(random_secret));
    StrHelper::strncpy(_prefs.password, generated, sizeof(_prefs.password));
    _cli.savePrefs(_fs);
  }
#endif
  acl.load(_fs, self_id);
"""
    s = replace_once(s, anchor, replacement, "unique V2 admin credential")

    anchor = """bool MyMesh::baseStationCongested() const {
  return p1_base_station_pool.getFreeCount() <= 6 ||
         p1_base_station_pool.getOutboundTotal() >= 24 ||
         p1_base_station_pool.getInboundTotal() >= 24;
}
"""
    replacement = """bool MyMesh::baseStationCongested() const {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  if (v2_background_suppressed) return true;
#endif
  return p1_base_station_pool.getFreeCount() <= 6 ||
         p1_base_station_pool.getOutboundTotal() >= 24 ||
         p1_base_station_pool.getInboundTotal() >= 24;
}
"""
    s = replace_once(s, anchor, replacement, "power-aware background suppression")

    insert_anchor = """bool MyMesh::baseStationCongested() const {
"""
    insert_at = s.find(insert_anchor)
    if insert_at < 0:
        fail("baseStationCongested insertion point missing")
    recovery_impl = r'''#if defined(MESH_OFFGRIDNL_P1PRO_V2)
int MyMesh::v2RecoverPacketQueues() {
  return p1_base_station_pool.recoverQueues();
}

bool MyMesh::v2RecoverRadio() {
  radio_driver.powerOff();
  delay(20);
  if (!radio_init()) return false;

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm > 22 ? 22 : _prefs.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  return true;
}
#endif

'''
    s = s[:insert_at] + recovery_impl + s[insert_at:]

    old_health = """  snprintf(reply, 160,
           "P1V1 free:%d tx:%d rx:%d drop:%lu/%lu peak:%u/%u air:%lu",
           baseStationFreePackets(), baseStationTxQueued(), baseStationRxQueued(),
           (unsigned long)baseStationDroppedTx(), (unsigned long)baseStationDroppedRx(),
           (unsigned)baseStationPeakTx(), (unsigned)baseStationPeakRx(),
           (unsigned long)getTotalAirTime());
"""
    new_health = """#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  snprintf(reply, 160,
           "P1V2 free:%d tx:%d rx:%d drop:%lu/%lu peak:%u/%u mesh:%u pwr:%u mv:%u",
           baseStationFreePackets(), baseStationTxQueued(), baseStationRxQueued(),
           (unsigned long)baseStationDroppedTx(), (unsigned long)baseStationDroppedRx(),
           (unsigned)baseStationPeakTx(), (unsigned)baseStationPeakRx(),
           (unsigned)getV2MeshPressureState(), (unsigned)getV2PowerState(),
           (unsigned)getV2BatteryMv());
#else
  snprintf(reply, 160,
           "P1V1 free:%d tx:%d rx:%d drop:%lu/%lu peak:%u/%u air:%lu",
           baseStationFreePackets(), baseStationTxQueued(), baseStationRxQueued(),
           (unsigned long)baseStationDroppedTx(), (unsigned long)baseStationDroppedRx(),
           (unsigned)baseStationPeakTx(), (unsigned)baseStationPeakRx(),
           (unsigned long)getTotalAirTime());
#endif
"""
    s = replace_once(s, old_health, new_health, "V2 health format")
    mesh_cpp.write_text(s)

    # Credential disclosure must never live in MyMesh::handleCommand because remote
    # admins supply their own message timestamp. Expose only a formatter and call it
    # from the physical Serial handler in main.cpp.
    s = mesh_h.read_text()
    anchor = """  bool getV2BackgroundSuppressed() const { return v2_background_suppressed; }
  int v2RecoverPacketQueues();
  bool v2RecoverRadio();
#endif
"""
    replacement = """  bool getV2BackgroundSuppressed() const { return v2_background_suppressed; }
  int v2RecoverPacketQueues();
  bool v2RecoverRadio();
  void formatV2LocalCredential(char* reply) const {
    snprintf(reply, 160, "credential:%s", _prefs.password);
  }
#endif
"""
    s = replace_once(s, anchor, replacement, "local credential formatter")
    mesh_h.write_text(s)


    # ------------------------------------------------------------------
    # Main loop: Solar Guardian runs alongside adaptive mesh + supervisor.
    # Credential readback is intercepted here, before MyMesh::handleCommand,
    # so it is physically local to the USB serial console.
    # ------------------------------------------------------------------
    s = main_cpp.read_text()
    serial_anchor = """#else
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
#endif
"""
    serial_replacement = """#else
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    if (strcmp(command, "base credential") == 0) {
      the_mesh.formatV2LocalCredential(reply);
    } else {
      the_mesh.handleCommand(0, command, reply);  // physical Serial CLI
    }
#else
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
#endif
#endif
"""
    s = replace_once(s, serial_anchor, serial_replacement, "USB-only credential route")
    s = replace_once(
        s,
        '#include "AdaptiveMeshController.h"\n',
        '#include "AdaptiveMeshController.h"\n#include "SolarGuardian.h"\n',
        "Solar Guardian include",
    )
    s = replace_once(
        s,
        """static AdaptiveMeshController v2_adaptive_mesh(the_mesh);
#endif
#endif
""",
        """static AdaptiveMeshController v2_adaptive_mesh(the_mesh);
static SolarGuardian v2_solar_guardian(board, the_mesh);
#endif
#endif
""",
        "Solar Guardian instance",
    )
    s = replace_once(
        s,
        """  v2_adaptive_mesh.begin();
#endif
#endif
}
""",
        """  v2_adaptive_mesh.begin();
  v2_solar_guardian.begin();
#endif
#endif
}
""",
        "Solar Guardian begin",
    )
    s = replace_once(
        s,
        """  v2_adaptive_mesh.loop();
#endif
#endif
  sensors.loop();
""",
        """  v2_adaptive_mesh.loop();
  v2_solar_guardian.loop();
#endif
#endif
  sensors.loop();
""",
        "Solar Guardian loop",
    )
    main_cpp.write_text(s)

    checks = {
        "variants/sensecap_solar/SenseCapSolarBoard.h": [
            "enterV2LowVoltageProtection",
            "SHUTDOWN_REASON_LOW_VOLTAGE",
        ],
        "src/helpers/StaticPoolPacketManager.cpp": [
            "recoverQueues()",
            "send_queue.removeByIdx(0)",
            "rx_queue.removeByIdx(0)",
        ],
        "examples/simple_repeater/MyMesh.cpp": [
            "random_secret[7]",
            "v2RecoverPacketQueues",
            "v2RecoverRadio",
            "P1V2 free:",
        ],
        "examples/simple_repeater/MyMesh.h": [
            "v2_power_airtime_factor",
            "setV2PowerPolicy",
            "v2_background_suppressed",
            "formatV2LocalCredential",
        ],
        "examples/simple_repeater/main.cpp": [
            "SolarGuardian",
            "v2_solar_guardian.loop()",
            'strcmp(command, "base credential") == 0',
            "formatV2LocalCredential",
        ],
        "examples/simple_repeater/BaseStationSupervisor.cpp": [
            "V2_RECOVERY_STAGE_MS",
            "v2RecoverPacketQueues",
            "v2RecoverRadio",
        ],
        "examples/simple_repeater/SolarGuardian.cpp": [
            "V2_POWER_BOOTLOCK_MV",
            "enterV2LowVoltageProtection",
            "setV2PowerPolicy",
        ],
    }
    for rel, markers in checks.items():
        data = (root / rel).read_text()
        for marker in markers:
            if marker not in data:
                fail(f"{rel}: missing marker {marker}")

    print("P1 Pro V2 phase 2 applied: Solar Guardian + staged recovery + unique credential")


if __name__ == "__main__":
    main()
