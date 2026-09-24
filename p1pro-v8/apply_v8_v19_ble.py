#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit("P1 Pro V8 V19 BLE patch failed: " + message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v8_v19_ble.py <MeshCore checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio_p = root / "variants/sensecap_solar/platformio.ini"
    mesh_p = root / "examples/companion_radio/MyMesh.cpp"

    for p in (pio_p, mesh_p):
        if not p.exists():
            fail("missing expected MeshCore file: " + str(p))

    # ------------------------------------------------------------------
    # Build identity: patch only SenseCap_Solar_companion_radio_ble.
    # ------------------------------------------------------------------
    pio = pio_p.read_text()
    env_start = pio.find("[env:SenseCap_Solar_companion_radio_ble]")
    env_end = pio.find("[env:SenseCap_Solar_companion_radio_usb]", env_start)
    if env_start < 0 or env_end < 0:
        fail("SenseCap Solar BLE companion environment missing")
    env = pio[env_start:env_end]
    env = replace_once(
        env,
        "  ${SenseCap_Solar.build_flags}\n",
        "  ${SenseCap_Solar.build_flags}\n  -D MESH_OFFGRIDNL_P1PRO_V8_V19=1\n",
        "V8 build flag",
    )
    pio = pio[:env_start] + env + pio[env_end:]
    pio_p.write_text(pio)

    mesh = mesh_p.read_text()

    # ------------------------------------------------------------------
    # V19 constants next to the companion protocol constants.
    # ------------------------------------------------------------------
    anchor = '#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="\n'
    replacement = '''#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="

#if defined(MESH_OFFGRIDNL_P1PRO_V8_V19)
static constexpr float V8_V19_FREQ_MHZ = 869.618f;
static constexpr float V8_V19_BW_KHZ = 62.5f;
static constexpr uint32_t V8_V19_FREQ_KHZ = 869618;
static constexpr uint32_t V8_V19_BW_HZ = 62500;
static constexpr uint8_t V8_V19_SF = 8;
static constexpr uint8_t V8_V19_CR = 5;
static constexpr int8_t V8_V19_TX_MAX_DBM = 22;
#endif
'''
    mesh = replace_once(mesh, anchor, replacement, "V19 constants")

    # ------------------------------------------------------------------
    # Fresh-install defaults: exact T-Deck V19 profile.
    # ------------------------------------------------------------------
    defaults_old = '''  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
'''
    defaults_new = '''#if defined(MESH_OFFGRIDNL_P1PRO_V8_V19)
  _prefs.freq = V8_V19_FREQ_MHZ;
  _prefs.sf = V8_V19_SF;
  _prefs.bw = V8_V19_BW_KHZ;
  _prefs.cr = V8_V19_CR;
  _prefs.tx_power_dbm = V8_V19_TX_MAX_DBM;
#else
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
#endif
'''
    mesh = replace_once(mesh, defaults_old, defaults_new, "V19 fresh defaults")

    # ------------------------------------------------------------------
    # Persisted prefs: force radio waveform back to V19 after load.
    # Keep any valid lower TX-power selection, but never permit >22 dBm.
    # ------------------------------------------------------------------
    load_old = '''  // load persisted prefs
  _store->loadPrefs(_prefs);
  sensors.node_lat = _prefs.node_lat;
'''
    load_new = '''  // load persisted prefs
  _store->loadPrefs(_prefs);
#if defined(MESH_OFFGRIDNL_P1PRO_V8_V19)
  const bool v8_v19_radio_changed =
      _prefs.freq < 869.6175f || _prefs.freq > 869.6185f ||
      _prefs.bw < 62.49f || _prefs.bw > 62.51f ||
      _prefs.sf != V8_V19_SF || _prefs.cr != V8_V19_CR ||
      _prefs.tx_power_dbm < -9 || _prefs.tx_power_dbm > V8_V19_TX_MAX_DBM;
  _prefs.freq = V8_V19_FREQ_MHZ;
  _prefs.bw = V8_V19_BW_KHZ;
  _prefs.sf = V8_V19_SF;
  _prefs.cr = V8_V19_CR;
  if (_prefs.tx_power_dbm < -9 || _prefs.tx_power_dbm > V8_V19_TX_MAX_DBM) {
    _prefs.tx_power_dbm = V8_V19_TX_MAX_DBM;
  }
  if (v8_v19_radio_changed) {
    savePrefs();
  }
#endif
  sensors.node_lat = _prefs.node_lat;
'''
    mesh = replace_once(mesh, load_old, load_new, "persisted V19 radio lock")

    # ------------------------------------------------------------------
    # MeshCore app radio command: only the exact V19 waveform is accepted.
    # Repeat remains upstream-controlled; V8 does not broaden repeat ranges.
    # ------------------------------------------------------------------
    radio_old = '''    if (repeat && !isValidClientRepeatFreq(freq)) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (freq >= 150000 && freq <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7000 &&
        bw <= 500000) {
      _prefs.sf = sf;
      _prefs.cr = cr;
      _prefs.freq = (float)freq / 1000.0;
      _prefs.bw = (float)bw / 1000.0;
      _prefs.setRepeatEn(repeat != 0);
      savePrefs();

      radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
      MESH_DEBUG_PRINTLN("OK: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);

      writeOKFrame();
    } else {
      MESH_DEBUG_PRINTLN("Error: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
'''
    radio_new = '''#if defined(MESH_OFFGRIDNL_P1PRO_V8_V19)
    const bool v8_v19_profile =
        freq == V8_V19_FREQ_KHZ && bw == V8_V19_BW_HZ &&
        sf == V8_V19_SF && cr == V8_V19_CR;

    if (!v8_v19_profile || (repeat && !isValidClientRepeatFreq(freq))) {
      MESH_DEBUG_PRINTLN("V8 V19 lock rejected radio params: f=%d, bw=%d, sf=%d, cr=%d, repeat=%d",
                         freq, bw, (uint32_t)sf, (uint32_t)cr, (uint32_t)repeat);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.freq = V8_V19_FREQ_MHZ;
      _prefs.bw = V8_V19_BW_KHZ;
      _prefs.sf = V8_V19_SF;
      _prefs.cr = V8_V19_CR;
      _prefs.setRepeatEn(repeat != 0);
      savePrefs();

      radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
      writeOKFrame();
    }
#else
    if (repeat && !isValidClientRepeatFreq(freq)) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (freq >= 150000 && freq <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7000 &&
        bw <= 500000) {
      _prefs.sf = sf;
      _prefs.cr = cr;
      _prefs.freq = (float)freq / 1000.0;
      _prefs.bw = (float)bw / 1000.0;
      _prefs.setRepeatEn(repeat != 0);
      savePrefs();

      radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
      MESH_DEBUG_PRINTLN("OK: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);

      writeOKFrame();
    } else {
      MESH_DEBUG_PRINTLN("Error: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
#endif
'''
    mesh = replace_once(mesh, radio_old, radio_new, "app radio hard lock")

    tx_old = '''  } else if (cmd_frame[0] == CMD_SET_RADIO_TX_POWER) {
    int8_t power = (int8_t)cmd_frame[1];
    if (power < -9 || power > MAX_LORA_TX_POWER) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.tx_power_dbm = power;
      savePrefs();
      radio_driver.setTxPower(_prefs.tx_power_dbm);
      writeOKFrame();
    }
'''
    tx_new = '''  } else if (cmd_frame[0] == CMD_SET_RADIO_TX_POWER) {
    int8_t power = (int8_t)cmd_frame[1];
#if defined(MESH_OFFGRIDNL_P1PRO_V8_V19)
    if (power < -9 || power > V8_V19_TX_MAX_DBM) {
#else
    if (power < -9 || power > MAX_LORA_TX_POWER) {
#endif
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.tx_power_dbm = power;
      savePrefs();
      radio_driver.setTxPower(_prefs.tx_power_dbm);
      writeOKFrame();
    }
'''
    mesh = replace_once(mesh, tx_old, tx_new, "TX ceiling")

    mesh_p.write_text(mesh)

    # Contract markers — fail immediately if an upstream anchor drifted.
    pio = pio_p.read_text()
    mesh = mesh_p.read_text()
    required = (
        "MESH_OFFGRIDNL_P1PRO_V8_V19=1",
        "V8_V19_FREQ_MHZ = 869.618f",
        "V8_V19_BW_KHZ = 62.5f",
        "V8_V19_FREQ_KHZ = 869618",
        "V8_V19_BW_HZ = 62500",
        "V8_V19_SF = 8",
        "V8_V19_CR = 5",
        "V8_V19_TX_MAX_DBM = 22",
        "v8_v19_radio_changed",
        "v8_v19_profile",
        "V8 V19 lock rejected radio params",
    )
    combined = pio + "\n" + mesh
    for marker in required:
        if marker not in combined:
            fail("missing marker " + marker)

    print("P1 Pro V8 V19 BLE patch applied")


if __name__ == "__main__":
    main()
