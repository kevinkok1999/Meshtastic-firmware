#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def fail(msg): raise SystemExit("P1 Pro V2 phase-2 patch failed: "+msg)
def once(text,old,new,label):
    n=text.count(old)
    if n!=1: fail(f"{label}: expected 1 anchor, found {n}")
    return text.replace(old,new,1)

def main():
    if len(sys.argv)!=2: fail("usage: apply_phase2.py <phase1-patched MeshCore>")
    root=pathlib.Path(sys.argv[1]).resolve()
    board_h=root/"variants/sensecap_solar/SenseCapSolarBoard.h"
    mesh_cpp=root/"examples/simple_repeater/MyMesh.cpp"
    mesh_h=root/"examples/simple_repeater/MyMesh.h"
    sup_h=root/"examples/simple_repeater/BaseStationSupervisor.h"
    sup_cpp=root/"examples/simple_repeater/BaseStationSupervisor.cpp"
    for p in (board_h,mesh_cpp,mesh_h,sup_h,sup_cpp):
        if not p.exists(): fail("missing "+str(p))

    # Expose one board-specific protective shutdown that reuses upstream's
    # LOW_VOLTAGE reason, LPCOMP wake and VBUS wake path.
    s=board_h.read_text()
    anchor="""  void powerOff() override {
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(LED_BLUE, LOW);

#ifdef PIN_USER_BTN
"""
    replacement="""#if defined(MESH_OFFGRIDNL_P1PRO_V2) && defined(NRF52_POWER_MANAGEMENT)
  void lowVoltageProtect() {
    shutdownPeripherals();
    initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE);
  }
#endif

  void powerOff() override {
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(LED_BLUE, LOW);

#ifdef PIN_USER_BTN
"""
    s=once(s,anchor,replacement,"SenseCAP low-voltage protection hook")
    board_h.write_text(s)

    s=mesh_h.read_text()
    s=once(s,
      "  bool _logging;\n",
      """  bool _logging;
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  uint8_t p1_power_state = 0;
#endif
""",
      "power state field")
    s=once(s,
      """  uint8_t baseStationPressureLevel() const;
  bool baseStationCongested() const;
  void formatBaseStationHealth(char* reply) const;
""",
      """  uint8_t baseStationPressureLevel() const;
  bool baseStationCongested() const;
  void setBaseStationPowerState(uint8_t state) { p1_power_state = state; }
  uint8_t baseStationPowerState() const { return p1_power_state; }
  bool baseStationRecoverRadio();
  void formatBaseStationHealth(char* reply) const;
""",
      "phase2 station API")
    s=once(s,
      """    return _prefs.cad_enabled || baseStationPressureLevel() >= 2;
""",
      """    return _prefs.cad_enabled ||
           (baseStationPowerState() == 0 && baseStationPressureLevel() >= 2);
""",
      "power-aware CAD AUTO")
    mesh_h.write_text(s)

    s=mesh_cpp.read_text()
    # First-boot trust: never leave the published upstream default password active.
    anchor="""#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  _prefs.freq = 869.618f;
  _prefs.bw = 62.5f;
  _prefs.sf = 8;
  _prefs.cr = 5;
  if (_prefs.tx_power_dbm > 22) _prefs.tx_power_dbm = 22;
#endif
#endif
  acl.load(_fs, self_id);
"""
    repl="""#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  _prefs.freq = 869.618f;
  _prefs.bw = 62.5f;
  _prefs.sf = 8;
  _prefs.cr = 5;
  if (_prefs.tx_power_dbm > 22) _prefs.tx_power_dbm = 22;

  // Replace only the published upstream factory default. Existing operator
  // credentials survive a V1 -> V2 upgrade.
  if (strcmp(_prefs.password, "password") == 0 || _prefs.password[0] == 0) {
    uint8_t entropy[8];
    char hex[17];
    getRNG()->random(entropy, sizeof(entropy));
    mesh::Utils::toHex(hex, entropy, sizeof(entropy));
    snprintf(_prefs.password, sizeof(_prefs.password), "P1-%s", hex);
    _cli.savePrefs(_fs);
  }
#endif
#endif
  acl.load(_fs, self_id);
"""
    s=once(s,anchor,repl,"unique admin first boot")

    s=once(s,
      """bool MyMesh::baseStationCongested() const {
  return baseStationPressureLevel() >= 1;
}
""",
      """bool MyMesh::baseStationCongested() const {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  // ECO/CRITICAL power states shed background adverts even if queues are quiet.
  return baseStationPressureLevel() >= 1 || baseStationPowerState() >= 1;
#else
  return baseStationPressureLevel() >= 1;
#endif
}
""",
      "power-aware background shedding")

    old="""void MyMesh::formatBaseStationHealth(char* reply) const {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  snprintf(reply, 180,
           "P1V2 p:%u free:%d tx:%d rx:%d drop:%lu/%lu peak:%u/%u air:%lu cad:%s",
           (unsigned)baseStationPressureLevel(),
           baseStationFreePackets(), baseStationTxQueued(), baseStationRxQueued(),
           (unsigned long)baseStationDroppedTx(), (unsigned long)baseStationDroppedRx(),
           (unsigned)baseStationPeakTx(), (unsigned)baseStationPeakRx(),
           (unsigned long)getTotalAirTime(),
           getCADEnabled() ? "on" : "off");
"""
    new="""bool MyMesh::baseStationRecoverRadio() {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  radio_driver.powerOff();
  delay(20);
  if (!radio_init()) return false;
  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  return true;
#else
  return false;
#endif
}

void MyMesh::formatBaseStationHealth(char* reply) const {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  snprintf(reply, 190,
           "P1V2 p:%u power:%u free:%d tx:%d rx:%d drop:%lu/%lu peak:%u/%u air:%lu cad:%s",
           (unsigned)baseStationPressureLevel(), (unsigned)baseStationPowerState(),
           baseStationFreePackets(), baseStationTxQueued(), baseStationRxQueued(),
           (unsigned long)baseStationDroppedTx(), (unsigned long)baseStationDroppedRx(),
           (unsigned)baseStationPeakTx(), (unsigned)baseStationPeakRx(),
           (unsigned long)getTotalAirTime(),
           getCADEnabled() ? "on" : "off");
"""
    s=once(s,old,new,"radio recovery + power health")
    mesh_cpp.write_text(s)

    s=sup_h.read_text()
    s=once(s,
      """  uint32_t congestion_events = 0;
  uint32_t pool_stall_recoveries = 0;
""",
      """  uint32_t congestion_events = 0;
  uint32_t pool_stall_recoveries = 0;
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  uint32_t next_power_check = 0;
  uint8_t power_state = 0; // 0 normal, 1 eco, 2 critical, 3 protect
  uint8_t low_voltage_samples = 0;
  bool soft_radio_recovery_attempted = false;
#endif
""",
      "solar/recovery fields")
    s=once(s,
      """  uint32_t getPoolStallRecoveries() const { return pool_stall_recoveries; }
};
""",
      """  uint32_t getPoolStallRecoveries() const { return pool_stall_recoveries; }
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  uint8_t getPowerState() const { return power_state; }
#endif
};
""",
      "power state getter")
    sup_h.write_text(s)

    s=sup_cpp.read_text()
    s=once(s,
      """#define P1_SUPERVISOR_INTERVAL_MS 5000UL
#define P1_POOL_STALL_REBOOT_MS 120000UL
""",
      """#define P1_SUPERVISOR_INTERVAL_MS 5000UL
#define P1_POOL_STALL_REBOOT_MS 120000UL
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
#define P1_POOL_SOFT_RECOVERY_MS 60000UL
#define P1_POWER_CHECK_MS 30000UL
#define P1_POWER_ECO_MV 3600
#define P1_POWER_ECO_RECOVER_MV 3700
#define P1_POWER_CRITICAL_MV 3450
#define P1_POWER_CRITICAL_RECOVER_MV 3550
#define P1_POWER_PROTECT_MV 3300
#endif
""",
      "phase2 thresholds")

    s=once(s,
      """  last_traffic_total =
      mesh.getNumSentFlood() + mesh.getNumSentDirect() +
      mesh.getNumRecvFlood() + mesh.getNumRecvDirect();
}
""",
      """  last_traffic_total =
      mesh.getNumSentFlood() + mesh.getNumSentDirect() +
      mesh.getNumRecvFlood() + mesh.getNumRecvDirect();
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  next_power_check = now;
  power_state = 0;
  low_voltage_samples = 0;
  soft_radio_recovery_attempted = false;
  mesh.setBaseStationPowerState(power_state);
#endif
}
""",
      "phase2 begin")

    marker="""  if (mesh.baseStationCongested()) {
    congestion_events++;
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    MESH_DEBUG_PRINTLN("P1 V2 pressure=%u free=%d tx=%d rx=%d",
        (unsigned)mesh.baseStationPressureLevel(), free_packets, tx_queued, rx_queued);
#endif
  }

"""
    add=marker+"""#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  if ((int32_t)(now - next_power_check) >= 0) {
    next_power_check = now + P1_POWER_CHECK_MS;
    const uint16_t mv = ::board.getBattMilliVolts();

    if (::board.isExternalPowered() || mv <= 1000) {
      power_state = 0;
      low_voltage_samples = 0;
    } else {
      if (mv < P1_POWER_PROTECT_MV) {
        if (low_voltage_samples < 255) low_voltage_samples++;
      } else {
        low_voltage_samples = 0;
      }

      if (low_voltage_samples >= 2) {
        power_state = 3;
        mesh.setBaseStationPowerState(power_state);
        MESH_DEBUG_PRINTLN("P1 V2 Solar Guardian: %u mV -> protective shutdown", mv);
        ::board.lowVoltageProtect();
        return;
      }

      if (power_state == 0) {
        if (mv < P1_POWER_ECO_MV) power_state = 1;
      } else if (power_state == 1) {
        if (mv >= P1_POWER_ECO_RECOVER_MV) power_state = 0;
        else if (mv < P1_POWER_CRITICAL_MV) power_state = 2;
      } else if (power_state == 2) {
        if (mv >= P1_POWER_CRITICAL_RECOVER_MV) power_state = 1;
      }
    }
    mesh.setBaseStationPowerState(power_state);
  }
#endif

"""
    s=once(s,marker,add,"Solar Guardian loop")

    old="""    if (pool_stalled && no_progress) {
      pool_stall_recoveries++;
      MESH_DEBUG_PRINTLN(
          "P1 supervisor: packet pool stalled (tx=%d rx=%d), rebooting",
          tx_queued, rx_queued);
      board.reboot();
    }
  } else {
    zero_free_since = 0;
  }
}
"""
    new="""#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    const bool soft_recovery_due =
        (uint32_t)(now - zero_free_since) >= P1_POOL_SOFT_RECOVERY_MS &&
        no_progress && !soft_radio_recovery_attempted;

    if (soft_recovery_due) {
      soft_radio_recovery_attempted = true;
      if (mesh.baseStationRecoverRadio()) {
        pool_stall_recoveries++;
        last_progress_at = now; // grant the recovered radio a fresh observation window
        MESH_DEBUG_PRINTLN("P1 V2 supervisor: soft radio recovery applied");
      }
    }

    if (pool_stalled && no_progress && soft_radio_recovery_attempted) {
      MESH_DEBUG_PRINTLN(
          "P1 V2 supervisor: hard stall remains (tx=%d rx=%d), rebooting",
          tx_queued, rx_queued);
      board.reboot();
    }
#else
    if (pool_stalled && no_progress) {
      pool_stall_recoveries++;
      MESH_DEBUG_PRINTLN(
          "P1 supervisor: packet pool stalled (tx=%d rx=%d), rebooting",
          tx_queued, rx_queued);
      board.reboot();
    }
#endif
  } else {
    zero_free_since = 0;
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    soft_radio_recovery_attempted = false;
#endif
  }
}
"""
    s=once(s,old,new,"staged recovery")
    sup_cpp.write_text(s)

    checks={
      "board":board_h.read_text(),
      "meshcpp":mesh_cpp.read_text(),
      "meshh":mesh_h.read_text(),
      "suph":sup_h.read_text(),
      "supcpp":sup_cpp.read_text(),
    }
    for m in ("lowVoltageProtect()","SHUTDOWN_REASON_LOW_VOLTAGE"):
        if m not in checks["board"]: fail("board power hook missing "+m)
    for m in ('strcmp(_prefs.password, "password") == 0',"getRNG()->random(entropy","P1-%s",
              "baseStationRecoverRadio()","power:%u"):
        if m not in checks["meshcpp"]: fail("trust/recovery marker missing "+m)
    for m in ("P1_POWER_PROTECT_MV 3300","low_voltage_samples >= 2","P1_POOL_SOFT_RECOVERY_MS 60000UL",
              "soft_radio_recovery_attempted","::board.lowVoltageProtect()"):
        if m not in checks["supcpp"]: fail("supervisor marker missing "+m)

    print("P1 Pro V2 phase 2 applied: Solar Guardian + staged recovery + unique admin")

if __name__=="__main__": main()
