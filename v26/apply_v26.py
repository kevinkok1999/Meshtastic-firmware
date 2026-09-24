#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit("V26 patch failed: " + message)


def one(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        fail(f"{label}: expected 1 anchor, found {count}")
    return source.replace(old, new, 1)


def section(source: str, heading: str, next_heading: str, old: str, new: str, label: str) -> str:
    start = source.find(heading)
    if start < 0:
        fail(label + ": section missing")
    end = source.find(next_heading, start + len(heading))
    if end < 0:
        end = len(source)
    chunk = source[start:end]
    chunk = one(chunk, old, new, label)
    return source[:start] + chunk + source[end:]


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v26.py <V19-patched checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio_path = root / "platformio.ini"
    main_path = root / "src/main.cpp"
    mesh_h_path = root / "src/MyMesh.h"
    mesh_cpp_path = root / "src/MyMesh.cpp"

    for path in (pio_path, main_path, mesh_h_path, mesh_cpp_path):
        if not path.exists():
            fail("missing " + str(path))

    pio = pio_path.read_text()
    if "MESH_OFFGRIDNL_V19=1" not in pio:
        fail("V19 base is not present")

    # V26 is intentionally T-Deck-only. V19 stays the proven Wi-Fi/mesh baseline;
    # this flag gates only the RF-maintenance layer.
    pio = section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V19=1\n",
        "  -D MESH_OFFGRIDNL_V19=1\n  -D MESH_OFFGRIDNL_V26=1\n",
        "V26 T-Deck build flag",
    )
    pio_path.write_text(pio)

    mesh_h = mesh_h_path.read_text()
    mesh_h = one(
        mesh_h,
        """  float getAirtimeBudgetFactor() const override;
  int getInterferenceThreshold() const override;
  int calcRxDelay(float score, uint32_t air_time) const override;
""",
        """  float getAirtimeBudgetFactor() const override;
  int getInterferenceThreshold() const override;
#if defined(MESH_OFFGRIDNL_V26)
  // V26: low-duty preventive receiver refresh. The SX126x wrapper itself
  // refuses to reset while a packet is actively being received.
  int getAGCResetInterval() const override;
#endif
  int calcRxDelay(float score, uint32_t air_time) const override;
""",
        "V26 AGC interval declaration",
    )
    mesh_h_path.write_text(mesh_h)

    mesh_cpp = mesh_cpp_path.read_text()
    mesh_cpp = one(
        mesh_cpp,
        """int MyMesh::getInterferenceThreshold() const {
  return 0; // disabled for now, until currentRSSI() problem is resolved
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
""",
        """int MyMesh::getInterferenceThreshold() const {
  return 0; // disabled for now, until currentRSSI() problem is resolved
}

#if defined(MESH_OFFGRIDNL_V26)
int MyMesh::getAGCResetInterval() const {
  // The current SX126x reset performs a full analog/calibration refresh, so V26
  // deliberately avoids the old 4-second repeater tuning. Five minutes costs
  // negligible receive duty while still preventing long-lived AGC deafness.
  return 5 * 60 * 1000;
}
#endif

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
""",
        "V26 preventive AGC interval",
    )
    mesh_cpp_path.write_text(mesh_cpp)

    main = main_path.read_text()
    guard = r'''
#if defined(MESH_OFFGRIDNL_V26)
// MeshOffGridNL V26 RF guard.
//
// V19 already enables SX1262 RX boosted gain and uses WadaMesh/MeshCore's
// rolling noise-floor calibration. V26 does NOT hop frequency, alter LoRa
// modem parameters or disable boosted gain. It only detects evidence that the
// receive chain has become unhealthy and asks the existing SX126x wrapper for
// a safe AGC/calibration refresh.
//
// Keep this independent from Serial logging: the T-Deck USB serial stream is a
// binary companion protocol, so unsolicited text can corrupt phone/PC frames.
namespace {
struct V26RfGuardState {
  bool initialized = false;
  uint32_t last_eval_ms = 0;
  uint32_t last_adaptive_reset_ms = 0;
  uint32_t good = 0;
  uint32_t bad = 0;
  uint32_t events = 0;
  uint32_t queue_drops = 0;
};

static V26RfGuardState s_v26_rf;

static constexpr uint32_t V26_RF_EVAL_MS = 15000;
static constexpr uint32_t V26_RF_BOOT_GRACE_MS = 60000;
static constexpr uint32_t V26_RF_MIN_ADAPTIVE_RESET_MS = 90000;
static constexpr uint32_t V26_RF_MISSED_EVENT_THRESHOLD = 4;
static constexpr uint32_t V26_RF_MIN_ERROR_PACKETS = 4;
static constexpr uint32_t V26_RF_ERROR_PERMILLE_THRESHOLD = 250;
static constexpr int V26_RF_BLOCKER_NOISE_DBM = -98;

static void v26RfMaintenance() {
  const uint32_t now = millis();

  if (s_v26_rf.initialized &&
      (uint32_t)(now - s_v26_rf.last_eval_ms) < V26_RF_EVAL_MS) {
    return;
  }

  const uint32_t good_now = radio_driver.getPacketsRecv();
  const uint32_t bad_now = radio_driver.getPacketsRecvErrors();
  const uint32_t events_now = radio_driver.getRxEvents();
  const uint32_t drops_now = radio_driver.getRxQueueDrops();

  if (!s_v26_rf.initialized) {
    s_v26_rf.initialized = true;
    s_v26_rf.last_eval_ms = now;
    s_v26_rf.good = good_now;
    s_v26_rf.bad = bad_now;
    s_v26_rf.events = events_now;
    s_v26_rf.queue_drops = drops_now;
    return;
  }

  const uint32_t good_delta = good_now - s_v26_rf.good;
  const uint32_t bad_delta = bad_now - s_v26_rf.bad;
  const uint32_t event_delta = events_now - s_v26_rf.events;
  const uint32_t drop_delta = drops_now - s_v26_rf.queue_drops;
  const uint32_t handled_delta = good_delta + bad_delta;
  const uint32_t missed_delta =
      event_delta > handled_delta ? event_delta - handled_delta : 0;
  const uint32_t error_permille =
      handled_delta ? (bad_delta * 1000UL) / handled_delta : 0;
  const int noise_floor = radio_driver.getNoiseFloor();

  // RX queue drops mean the host/UI consumer stalled. Resetting the RF frontend
  // cannot fix that class of loss, so explicitly keep it out of the RF trigger.
  const bool consumer_pressure = drop_delta != 0;
  const bool decode_pressure =
      handled_delta >= V26_RF_MIN_ERROR_PACKETS &&
      bad_delta >= 2 &&
      error_permille >= V26_RF_ERROR_PERMILLE_THRESHOLD;
  const bool missed_event_pressure =
      event_delta >= V26_RF_MISSED_EVENT_THRESHOLD &&
      missed_delta >= V26_RF_MISSED_EVENT_THRESHOLD;
  const bool blocker_pressure =
      noise_floor < 0 &&
      noise_floor >= V26_RF_BLOCKER_NOISE_DBM &&
      bad_delta >= 2;

  const bool recovery_pressure =
      !consumer_pressure &&
      (decode_pressure || missed_event_pressure || blocker_pressure);

  const bool past_boot_grace = now >= V26_RF_BOOT_GRACE_MS;
  const bool adaptive_cooldown_ok =
      s_v26_rf.last_adaptive_reset_ms == 0 ||
      (uint32_t)(now - s_v26_rf.last_adaptive_reset_ms) >=
          V26_RF_MIN_ADAPTIVE_RESET_MS;

  if (recovery_pressure && past_boot_grace && adaptive_cooldown_ok) {
    // Be stricter than resetAGC() itself: only touch the radio while the mesh
    // says it is in RX and no preamble/header is currently active.
    if (radio_driver.isInRecvMode() && !radio_driver.isReceiving()) {
      radio_driver.resetAGC();
      radio_driver.triggerNoiseFloorCalibrate(0);
      s_v26_rf.last_adaptive_reset_ms = now;
    }
  }

  s_v26_rf.last_eval_ms = now;
  s_v26_rf.good = good_now;
  s_v26_rf.bad = bad_now;
  s_v26_rf.events = events_now;
  s_v26_rf.queue_drops = drops_now;
}
} // namespace
#endif
'''

    main = one(
        main,
        """void loop() {
""",
        guard + """
void loop() {
""",
        "V26 RF guard definition",
    )

    main = one(
        main,
        """#ifdef DISPLAY_CLASS
  STALL_SCOPE("mesh", the_mesh.loop());
#else
  the_mesh.loop();
#endif
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
""",
        """#ifdef DISPLAY_CLASS
  STALL_SCOPE("mesh", the_mesh.loop());
#else
  the_mesh.loop();
#endif
#if defined(MESH_OFFGRIDNL_V26)
  // Spectrum owns and retunes the raw SX1262 while its app is open; V26 must
  // never run RF maintenance during that ownership window.
  #if defined(HAS_TOUCH_UI)
  if (!spectrumOwnsRadio()) v26RfMaintenance();
  #else
  v26RfMaintenance();
  #endif
#endif
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
""",
        "V26 RF guard loop hook",
    )
    main_path.write_text(main)

    # Final fail-closed verification. V26 must preserve V19's exact modem/channel
    # assumptions and add no spectral-scan or frequency-hopping side effects.
    pio = pio_path.read_text()
    main = main_path.read_text()
    mesh_h = mesh_h_path.read_text()
    mesh_cpp = mesh_cpp_path.read_text()

    required = (
        "MESH_OFFGRIDNL_V19=1",
        "MESH_OFFGRIDNL_V26=1",
        "V26_RF_EVAL_MS = 15000",
        "V26_RF_MIN_ADAPTIVE_RESET_MS = 90000",
        "radio_driver.getPacketsRecvErrors()",
        "radio_driver.getRxEvents()",
        "radio_driver.getRxQueueDrops()",
        "radio_driver.getNoiseFloor()",
        "radio_driver.isInRecvMode()",
        "!radio_driver.isReceiving()",
        "radio_driver.resetAGC()",
        "radio_driver.triggerNoiseFloorCalibrate(0)",
        "return 5 * 60 * 1000;",
    )
    joined = "\n".join((pio, main, mesh_h, mesh_cpp))
    for marker in required:
        if marker not in joined:
            fail("missing V26 marker " + marker)

    v26_start = main.find("#if defined(MESH_OFFGRIDNL_V26)\n// MeshOffGridNL V26 RF guard.")
    v26_end = main.find("\n#endif\n\nvoid loop()", v26_start)
    if v26_start < 0 or v26_end < 0:
        fail("V26 RF guard block missing")
    block = main[v26_start:v26_end]

    for forbidden in (
        "setFrequency(",
        "setBandwidth(",
        "setSpreadingFactor(",
        "setCodingRate(",
        "spectralScan",
        "setRxBoostedGainMode(false",
        "WiFi.",
    ):
        if forbidden in block:
            fail("V26 RF guard contains forbidden behavior " + forbidden)

    print("V26 applied: V19 baseline + conservative SX1262 RF guard + preventive AGC refresh")


if __name__ == "__main__":
    main()
