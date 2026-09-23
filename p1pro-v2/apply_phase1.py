#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def fail(msg: str) -> None:
    raise SystemExit("P1 Pro V2 phase-1 patch failed: " + msg)

def once(text: str, old: str, new: str, label: str) -> str:
    n=text.count(old)
    if n != 1:
        fail(f"{label}: expected 1 anchor, found {n}")
    return text.replace(old,new,1)

def main() -> None:
    if len(sys.argv)!=2:
        fail("usage: apply_phase1.py <V1+phase2 patched MeshCore>")
    root=pathlib.Path(sys.argv[1]).resolve()
    variant=root/"variants/sensecap_solar/platformio.ini"
    mesh_cpp=root/"examples/simple_repeater/MyMesh.cpp"
    mesh_h=root/"examples/simple_repeater/MyMesh.h"
    cli=root/"src/helpers/CommonCLI.cpp"
    supervisor=root/"examples/simple_repeater/BaseStationSupervisor.cpp"
    for p in (variant,mesh_cpp,mesh_h,cli,supervisor):
        if not p.exists(): fail("missing "+str(p))

    # Dedicated V2 compile marker. V1 marker stays enabled because V2 deliberately
    # inherits the already-tested V1 base/resilience implementation.
    s=variant.read_text()
    s=once(s,
      "  -D MESH_OFFGRIDNL_P1PRO_V1=1\n",
      "  -D MESH_OFFGRIDNL_P1PRO_V1=1\n  -D MESH_OFFGRIDNL_P1PRO_V2=1\n",
      "V2 build marker")
    variant.write_text(s)

    # Persisted configs may come from older MeshCore/V1 installs. V2 is hard locked
    # back to the proven 868/V19 profile before the radio is configured.
    s=mesh_cpp.read_text()
    anchor="""  _prefs.path_hash_mode = 0;
  // Do not rely on loop_detect for one-byte paths: collisions are possible.
  // Hop ceilings + duplicate suppression remain the primary safety barriers.
#endif
  acl.load(_fs, self_id);
"""
    repl="""  _prefs.path_hash_mode = 0;
  // Do not rely on loop_detect for one-byte paths: collisions are possible.
  // Hop ceilings + duplicate suppression remain the primary safety barriers.
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  _prefs.freq = 869.618f;
  _prefs.bw = 62.5f;
  _prefs.sf = 8;
  _prefs.cr = 5;
  if (_prefs.tx_power_dbm > 22) _prefs.tx_power_dbm = 22;
#endif
#endif
  acl.load(_fs, self_id);
"""
    s=once(s,anchor,repl,"persisted EU868 hard lock")

    old="""bool MyMesh::baseStationCongested() const {
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
"""
    new="""uint8_t MyMesh::baseStationPressureLevel() const {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  const int free_packets = p1_base_station_pool.getFreeCount();
  const int tx = p1_base_station_pool.getOutboundTotal();
  const int rx = p1_base_station_pool.getInboundTotal();
  if (free_packets <= 4 || tx >= 26 || rx >= 26) return 3; // CRITICAL
  if (free_packets <= 8 || tx >= 20 || rx >= 20) return 2; // SEVERE
  if (free_packets <= 14 || tx >= 12 || rx >= 12) return 1; // BUSY
  return 0; // NORMAL
#else
  return (p1_base_station_pool.getFreeCount() <= 6 ||
          p1_base_station_pool.getOutboundTotal() >= 24 ||
          p1_base_station_pool.getInboundTotal() >= 24) ? 1 : 0;
#endif
}

bool MyMesh::baseStationCongested() const {
  return baseStationPressureLevel() >= 1;
}

void MyMesh::formatBaseStationHealth(char* reply) const {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  snprintf(reply, 180,
           "P1V2 p:%u free:%d tx:%d rx:%d drop:%lu/%lu peak:%u/%u air:%lu cad:%s",
           (unsigned)baseStationPressureLevel(),
           baseStationFreePackets(), baseStationTxQueued(), baseStationRxQueued(),
           (unsigned long)baseStationDroppedTx(), (unsigned long)baseStationDroppedRx(),
           (unsigned)baseStationPeakTx(), (unsigned)baseStationPeakRx(),
           (unsigned long)getTotalAirTime(),
           getCADEnabled() ? "on" : "off");
#else
  snprintf(reply, 160,
           "P1V1 free:%d tx:%d rx:%d drop:%lu/%lu peak:%u/%u air:%lu",
           baseStationFreePackets(), baseStationTxQueued(), baseStationRxQueued(),
           (unsigned long)baseStationDroppedTx(), (unsigned long)baseStationDroppedRx(),
           (unsigned)baseStationPeakTx(), (unsigned)baseStationPeakRx(),
           (unsigned long)getTotalAirTime());
#endif
}
"""
    s=once(s,old,new,"adaptive pressure health")
    mesh_cpp.write_text(s)

    s=mesh_h.read_text()
    s=once(s,
      "  bool baseStationCongested() const;\n",
      "  uint8_t baseStationPressureLevel() const;\n  bool baseStationCongested() const;\n",
      "pressure declaration")
    oldcad="""  bool getCADEnabled() const override {
    return _prefs.cad_enabled;
  }
"""
    newcad="""  bool getCADEnabled() const override {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    // Operator 'cad on' remains respected. AUTO engages only under meaningful
    // bounded-queue pressure and falls back off when pressure clears.
    return _prefs.cad_enabled || baseStationPressureLevel() >= 2;
#else
    return _prefs.cad_enabled;
#endif
  }
"""
    s=once(s,oldcad,newcad,"CAD AUTO")
    mesh_h.write_text(s)

    # Lock all CLI paths that can move the repeater away from EU868/V19.
    s=cli.read_text()

    old="""      if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0) {
        _callbacks->applyTempRadioParams(freq, bw, sf, cr, temp_timeout_mins);
        sprintf(reply, "OK - temp params for %d mins", temp_timeout_mins);
      } else {
        strcpy(reply, "Error, invalid params");
      }
"""
    new="""#if defined(MESH_OFFGRIDNL_P1PRO_V2)
      if (freq > 869.617f && freq < 869.619f &&
          bw > 62.49f && bw < 62.51f && sf == 8 && cr == 5 &&
          temp_timeout_mins > 0) {
        _callbacks->applyTempRadioParams(869.618f, 62.5f, 8, 5, temp_timeout_mins);
        sprintf(reply, "OK - locked EU868 temp params for %d mins", temp_timeout_mins);
      } else {
        strcpy(reply, "Error: P1 Pro V2 is locked to 869.618/62.5/SF8/CR5");
      }
#else
      if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0) {
        _callbacks->applyTempRadioParams(freq, bw, sf, cr, temp_timeout_mins);
        sprintf(reply, "OK - temp params for %d mins", temp_timeout_mins);
      } else {
        strcpy(reply, "Error, invalid params");
      }
#endif
"""
    s=once(s,old,new,"tempradio EU868 lock")

    old="""    if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
      _prefs->sf = sf;
      _prefs->cr = cr;
      _prefs->freq = freq;
      _prefs->bw = bw;
      _callbacks->savePrefs();
      strcpy(reply, "OK - reboot to apply");
    } else {
      strcpy(reply, "Error, invalid radio params");
    }
"""
    new="""#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    if (freq > 869.617f && freq < 869.619f &&
        bw > 62.49f && bw < 62.51f && sf == 8 && cr == 5) {
      _prefs->sf = 8;
      _prefs->cr = 5;
      _prefs->freq = 869.618f;
      _prefs->bw = 62.5f;
      _callbacks->savePrefs();
      strcpy(reply, "OK - EU868/V19 profile locked");
    } else {
      strcpy(reply, "Error: P1 Pro V2 is EU868/V19 locked");
    }
#else
    if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
      _prefs->sf = sf;
      _prefs->cr = cr;
      _prefs->freq = freq;
      _prefs->bw = bw;
      _callbacks->savePrefs();
      strcpy(reply, "OK - reboot to apply");
    } else {
      strcpy(reply, "Error, invalid radio params");
    }
#endif
"""
    s=once(s,old,new,"set radio EU868 lock")

    old="""  } else if (memcmp(config, "tx ", 3) == 0) {
    _prefs->tx_power_dbm = atoi(&config[3]);
    savePrefs();
    _callbacks->setTxPower(_prefs->tx_power_dbm);
    strcpy(reply, "OK");
  } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
    _prefs->freq = atof(&config[5]);
    savePrefs();
    strcpy(reply, "OK - reboot to apply");
"""
    new="""  } else if (memcmp(config, "tx ", 3) == 0) {
    int requested = atoi(&config[3]);
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    if (requested < -9 || requested > 22) {
      strcpy(reply, "Error: P1 Pro V2 TX range is -9..22 dBm");
    } else {
      _prefs->tx_power_dbm = requested;
      savePrefs();
      _callbacks->setTxPower(_prefs->tx_power_dbm);
      strcpy(reply, "OK");
    }
#else
    _prefs->tx_power_dbm = requested;
    savePrefs();
    _callbacks->setTxPower(_prefs->tx_power_dbm);
    strcpy(reply, "OK");
#endif
  } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    float requested = atof(&config[5]);
    if (requested > 869.617f && requested < 869.619f) {
      _prefs->freq = 869.618f;
      savePrefs();
      strcpy(reply, "OK - EU868/V19 frequency locked");
    } else {
      strcpy(reply, "Error: P1 Pro V2 frequency is locked to 869.618 MHz");
    }
#else
    _prefs->freq = atof(&config[5]);
    savePrefs();
    strcpy(reply, "OK - reboot to apply");
#endif
"""
    s=once(s,old,new,"TX/frequency lock")
    cli.write_text(s)

    # V2 supervisor records pressure but keeps V1's conservative hard-stall reboot.
    s=supervisor.read_text()
    s=once(s,
      """  if (mesh.baseStationCongested()) {
    congestion_events++;
  }
""",
      """  if (mesh.baseStationCongested()) {
    congestion_events++;
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    MESH_DEBUG_PRINTLN("P1 V2 pressure=%u free=%d tx=%d rx=%d",
        (unsigned)mesh.baseStationPressureLevel(), free_packets, tx_queued, rx_queued);
#endif
  }
""",
      "V2 pressure telemetry")
    supervisor.write_text(s)

    # Drift guards.
    data={
      "variant":variant.read_text(),
      "meshcpp":mesh_cpp.read_text(),
      "meshh":mesh_h.read_text(),
      "cli":cli.read_text(),
      "sup":supervisor.read_text(),
    }
    for marker in (
      "MESH_OFFGRIDNL_P1PRO_V2=1",
      "-D LORA_FREQ=869.618","-D LORA_BW=62.5","-D LORA_SF=8","-D LORA_CR=5","-D LORA_TX_POWER=22"
    ):
      if marker not in data["variant"]: fail("missing build contract "+marker)
    for marker in ("_prefs.freq = 869.618f;","baseStationPressureLevel()","baseStationPressureLevel() >= 2"):
      if marker not in data["meshcpp"]+data["meshh"]: fail("missing adaptive contract "+marker)
    for marker in ("P1 Pro V2 is EU868/V19 locked","P1 Pro V2 TX range is -9..22 dBm","locked to 869.618 MHz"):
      if marker not in data["cli"]: fail("missing radio lock "+marker)

    print("P1 Pro V2 phase 1 applied: EU868 hard lock + adaptive pressure/CAD")

if __name__=="__main__":
    main()
