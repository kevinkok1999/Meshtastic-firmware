#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import shutil
import sys


def fail(message: str) -> None:
    raise SystemExit("P1 Pro V2 phase-1 patch failed: " + message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_phase1.py <V1-phase2-patched MeshCore checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    here = pathlib.Path(__file__).resolve().parent

    variant = root / "variants/sensecap_solar/platformio.ini"
    mesh_h = root / "examples/simple_repeater/MyMesh.h"
    mesh_cpp = root / "examples/simple_repeater/MyMesh.cpp"
    main_cpp = root / "examples/simple_repeater/main.cpp"
    common_cpp = root / "src/helpers/CommonCLI.cpp"

    for p in (variant, mesh_h, mesh_cpp, main_cpp, common_cpp):
        if not p.exists():
            fail("missing expected MeshCore file: " + str(p))

    overlay_dir = here / "phase1-overlay"
    for src in overlay_dir.rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(overlay_dir)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    # ------------------------------------------------------------------
    # Build identity: V2 inherits the proven V1 resilience substrate.
    # ------------------------------------------------------------------
    s = variant.read_text()
    s = replace_once(
        s,
        "  -D MESH_OFFGRIDNL_P1PRO_ADVERT_MAX=8\n",
        """  -D MESH_OFFGRIDNL_P1PRO_ADVERT_MAX=8
  -D MESH_OFFGRIDNL_P1PRO_V2=1
  -D MESH_OFFGRIDNL_P1PRO_EU868_LOCK=1
""",
        "V2 build markers",
    )
    variant.write_text(s)

    # ------------------------------------------------------------------
    # MyMesh: conservative 10% ceiling + adaptive policy hooks.
    # ------------------------------------------------------------------
    s = mesh_cpp.read_text()
    s = replace_once(
        s,
        "  _prefs.airtime_factor = 1.0;\n",
        """#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  // 10% maximum duty-cycle budget. Larger factors are stricter.
  _prefs.airtime_factor = 9.0f;
#else
  _prefs.airtime_factor = 1.0;
#endif
""",
        "V2 airtime baseline",
    )

    lock_anchor = """  // Hop ceilings + duplicate suppression remain the primary safety barriers.
#endif
  acl.load(_fs, self_id);
"""
    lock_replacement = """  // Hop ceilings + duplicate suppression remain the primary safety barriers.
#endif
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  // V2 is intentionally EU868 + T-Deck V19 compatible only.
  _prefs.freq = 869.618f;
  _prefs.bw = 62.5f;
  _prefs.sf = 8;
  _prefs.cr = 5;
  if (_prefs.tx_power_dbm > 22) _prefs.tx_power_dbm = 22;
  // Never relax a stricter user-set airtime limit.
  if (_prefs.airtime_factor < 9.0f) _prefs.airtime_factor = 9.0f;
#endif
  acl.load(_fs, self_id);
"""
    s = replace_once(s, lock_anchor, lock_replacement, "persisted EU868 lock")

    temp_old = """void MyMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) {
  set_radio_at = futureMillis(2000); // give CLI reply some time to be sent back, before applying temp radio params
  pending_freq = freq;
  pending_bw = bw;
  pending_sf = sf;
  pending_cr = cr;

  revert_radio_at = futureMillis(2000 + timeout_mins * 60 * 1000); // schedule when to revert radio params
}
"""
    temp_new = """void MyMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  // Defense in depth: CommonCLI also rejects non-V19 radio parameters.
  const bool v2_profile =
      freq > 869.617f && freq < 869.619f &&
      bw > 62.49f && bw < 62.51f &&
      sf == 8 && cr == 5;
  if (!v2_profile) return;
#endif
  set_radio_at = futureMillis(2000); // give CLI reply some time to be sent back, before applying temp radio params
  pending_freq = freq;
  pending_bw = bw;
  pending_sf = sf;
  pending_cr = cr;

  revert_radio_at = futureMillis(2000 + timeout_mins * 60 * 1000); // schedule when to revert radio params
}
"""
    s = replace_once(s, temp_old, temp_new, "temporary radio lock")

    s = replace_once(
        s,
        """void MyMesh::setTxPower(int8_t power_dbm) {
  radio_driver.setTxPower(power_dbm);
}
""",
        """void MyMesh::setTxPower(int8_t power_dbm) {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  if (power_dbm > 22) power_dbm = 22;
#endif
  radio_driver.setTxPower(power_dbm);
}
""",
        "TX ceiling",
    )
    mesh_cpp.write_text(s)

    s = mesh_h.read_text()
    s = replace_once(
        s,
        "  int  matching_peer_indexes[MAX_CLIENTS];\n",
        """  int  matching_peer_indexes[MAX_CLIENTS];
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  float v2_adaptive_airtime_factor = 9.0f;
  bool v2_adaptive_cad = false;
  uint8_t v2_mesh_pressure_state = 0;
#endif
""",
        "adaptive members",
    )
    s = replace_once(
        s,
        """  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }
""",
        """  float getAirtimeBudgetFactor() const override {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    return _prefs.airtime_factor > v2_adaptive_airtime_factor
        ? _prefs.airtime_factor : v2_adaptive_airtime_factor;
#else
    return _prefs.airtime_factor;
#endif
  }
""",
        "adaptive airtime getter",
    )
    s = replace_once(
        s,
        """  bool getCADEnabled() const override {
    return _prefs.cad_enabled;
  }
""",
        """  bool getCADEnabled() const override {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    return _prefs.cad_enabled || v2_adaptive_cad;
#else
    return _prefs.cad_enabled;
#endif
  }
""",
        "adaptive CAD getter",
    )
    s = replace_once(
        s,
        """  bool setRxBoostedGain(bool enable) override;

  #if defined(USE_LR2021)
""",
        """  bool setRxBoostedGain(bool enable) override;

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

#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  void setV2AdaptiveMeshPolicy(float airtime_factor, bool cad, uint8_t pressure_state) {
    v2_adaptive_airtime_factor = airtime_factor < 9.0f ? 9.0f : airtime_factor;
    v2_adaptive_cad = cad;
    v2_mesh_pressure_state = pressure_state;
  }
  float getV2AdaptiveAirtimeFactor() const { return v2_adaptive_airtime_factor; }
  bool getV2AdaptiveCad() const { return v2_adaptive_cad; }
  uint8_t getV2MeshPressureState() const { return v2_mesh_pressure_state; }
#endif

  #if defined(USE_LR2021)
""",
        "adaptive public API",
    )
    mesh_h.write_text(s)

    # ------------------------------------------------------------------
    # CLI: prevent accidental band/profile escape on V2.
    # ------------------------------------------------------------------
    s = common_cpp.read_text()

    temp_cli_old = """      if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0) {
        _callbacks->applyTempRadioParams(freq, bw, sf, cr, temp_timeout_mins);
        sprintf(reply, "OK - temp params for %d mins", temp_timeout_mins);
      } else {
        strcpy(reply, "Error, invalid params");
      }
"""
    temp_cli_new = """#if defined(MESH_OFFGRIDNL_P1PRO_V2)
      const bool valid_temp =
          freq > 869.617f && freq < 869.619f &&
          bw > 62.49f && bw < 62.51f &&
          sf == 8 && cr == 5 && temp_timeout_mins > 0;
#else
      const bool valid_temp =
          freq >= 150.0f && freq <= 2500.0f &&
          sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 &&
          bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0;
#endif
      if (valid_temp) {
        _callbacks->applyTempRadioParams(freq, bw, sf, cr, temp_timeout_mins);
        sprintf(reply, "OK - temp params for %d mins", temp_timeout_mins);
      } else {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
        strcpy(reply, "Error, V2 is locked to 869.618/62.5/SF8/CR5");
#else
        strcpy(reply, "Error, invalid params");
#endif
      }
"""
    s = replace_once(s, temp_cli_old, temp_cli_new, "tempradio CLI lock")

    radio_cli_old = """    if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
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
    radio_cli_new = """#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    const bool valid_radio =
        freq > 869.617f && freq < 869.619f &&
        bw > 62.49f && bw < 62.51f &&
        sf == 8 && cr == 5;
#else
    const bool valid_radio =
        freq >= 150.0f && freq <= 2500.0f &&
        sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 &&
        bw >= 7.0f && bw <= 500.0f;
#endif
    if (valid_radio) {
      _prefs->sf = sf;
      _prefs->cr = cr;
      _prefs->freq = freq;
      _prefs->bw = bw;
      _callbacks->savePrefs();
      strcpy(reply, "OK - reboot to apply");
    } else {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
      strcpy(reply, "Error, V2 is locked to 869.618/62.5/SF8/CR5");
#else
      strcpy(reply, "Error, invalid radio params");
#endif
    }
"""
    s = replace_once(s, radio_cli_old, radio_cli_new, "radio CLI lock")

    tx_old = """  } else if (memcmp(config, "tx ", 3) == 0) {
    _prefs->tx_power_dbm = atoi(&config[3]);
    savePrefs();
    _callbacks->setTxPower(_prefs->tx_power_dbm);
    strcpy(reply, "OK");
  } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
    _prefs->freq = atof(&config[5]);
    savePrefs();
    strcpy(reply, "OK - reboot to apply");
"""
    tx_new = """  } else if (memcmp(config, "tx ", 3) == 0) {
    int power = atoi(&config[3]);
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    if (power < -9 || power > 22) {
      strcpy(reply, "Error, V2 TX range is -9..22 dBm");
    } else {
      _prefs->tx_power_dbm = power;
      savePrefs();
      _callbacks->setTxPower(_prefs->tx_power_dbm);
      strcpy(reply, "OK");
    }
#else
    _prefs->tx_power_dbm = power;
    savePrefs();
    _callbacks->setTxPower(_prefs->tx_power_dbm);
    strcpy(reply, "OK");
#endif
  } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
    strcpy(reply, "Error, V2 frequency is locked to 869.618 MHz");
#else
    _prefs->freq = atof(&config[5]);
    savePrefs();
    strcpy(reply, "OK - reboot to apply");
#endif
"""
    s = replace_once(s, tx_old, tx_new, "TX/frequency CLI lock")
    common_cpp.write_text(s)

    # ------------------------------------------------------------------
    # Adaptive controller: pressure-aware airtime/CAD with hysteresis.
    # ------------------------------------------------------------------
    s = main_cpp.read_text()
    s = replace_once(
        s,
        '#include "BaseStationSupervisor.h"\n',
        '#include "BaseStationSupervisor.h"\n'
        '#if defined(MESH_OFFGRIDNL_P1PRO_V2)\n'
        '#include "AdaptiveMeshController.h"\n'
        '#endif\n',
        "adaptive include",
    )
    s = replace_once(
        s,
        """static BaseStationSupervisor base_station_supervisor(board, the_mesh);
#endif
""",
        """static BaseStationSupervisor base_station_supervisor(board, the_mesh);
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
static AdaptiveMeshController v2_adaptive_mesh(the_mesh);
#endif
#endif
""",
        "adaptive instance",
    )
    s = replace_once(
        s,
        """  base_station_supervisor.begin();
#endif
}
""",
        """  base_station_supervisor.begin();
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  v2_adaptive_mesh.begin();
#endif
#endif
}
""",
        "adaptive begin",
    )
    s = replace_once(
        s,
        """  base_station_supervisor.loop();
#endif
  sensors.loop();
""",
        """  base_station_supervisor.loop();
#if defined(MESH_OFFGRIDNL_P1PRO_V2)
  v2_adaptive_mesh.loop();
#endif
#endif
  sensors.loop();
""",
        "adaptive loop",
    )
    main_cpp.write_text(s)

    checks = {
        "variants/sensecap_solar/platformio.ini": [
            "MESH_OFFGRIDNL_P1PRO_V2=1",
            "MESH_OFFGRIDNL_P1PRO_EU868_LOCK=1",
            "LORA_FREQ=869.618",
        ],
        "examples/simple_repeater/MyMesh.cpp": [
            "_prefs.airtime_factor = 9.0f",
            "_prefs.freq = 869.618f",
            "if (_prefs.tx_power_dbm > 22)",
        ],
        "examples/simple_repeater/MyMesh.h": [
            "v2_adaptive_airtime_factor",
            "setV2AdaptiveMeshPolicy",
            "_prefs.cad_enabled || v2_adaptive_cad",
        ],
        "src/helpers/CommonCLI.cpp": [
            "V2 is locked to 869.618/62.5/SF8/CR5",
            "V2 TX range is -9..22 dBm",
            "V2 frequency is locked to 869.618 MHz",
        ],
        "examples/simple_repeater/main.cpp": [
            "AdaptiveMeshController",
            "v2_adaptive_mesh.loop()",
        ],
    }
    for rel, markers in checks.items():
        data = (root / rel).read_text()
        for marker in markers:
            if marker not in data:
                fail(f"{rel}: missing marker {marker}")

    print("P1 Pro V2 phase 1 applied: EU868/V19 hard lock + adaptive airtime/CAD")


if __name__ == "__main__":
    main()
