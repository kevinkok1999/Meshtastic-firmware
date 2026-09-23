#!/usr/bin/env python3
from __future__ import annotations
import pathlib,sys

def die(x): raise SystemExit("P1 Pro V2 phase-2 contract failure: "+x)

def main():
    if len(sys.argv)!=2: die("usage: test_phase2.py <patched MeshCore>")
    root=pathlib.Path(sys.argv[1])
    board=(root/"variants/sensecap_solar/SenseCapSolarBoard.h").read_text()
    cpp=(root/"examples/simple_repeater/MyMesh.cpp").read_text()
    h=(root/"examples/simple_repeater/MyMesh.h").read_text()
    sh=(root/"examples/simple_repeater/BaseStationSupervisor.h").read_text()
    sc=(root/"examples/simple_repeater/BaseStationSupervisor.cpp").read_text()

    # Hard safety threshold must match SenseCAP upstream boot protection.
    variant=(root/"variants/sensecap_solar/variant.h").read_text()
    assert "PWRMGT_VOLTAGE_BOOTLOCK (3300)" in variant
    for m in ("#define P1_POWER_PROTECT_MV 3300","low_voltage_samples >= 2",
              "::board.lowVoltageProtect()","SHUTDOWN_REASON_LOW_VOLTAGE"):
        if m not in sc+"\n"+board: die("low-voltage contract missing "+m)

    # Load shedding has hysteresis and does not disable the LoRa dataplane.
    for m in ("P1_POWER_ECO_MV 3600","P1_POWER_ECO_RECOVER_MV 3700",
              "P1_POWER_CRITICAL_MV 3450","P1_POWER_CRITICAL_RECOVER_MV 3550",
              "setBaseStationPowerState"):
        if m not in sc+"\n"+h: die("power-state contract missing "+m)
    if "disable_fwd = 1" in sc: die("Solar Guardian must not disable routing")

    # Published upstream default admin credential is replaced locally on first boot.
    for m in ('strcmp(_prefs.password, "password") == 0',
              "getRNG()->random(entropy", 'snprintf(_prefs.password', '"P1-%s"', "_cli.savePrefs(_fs)"):
        if m not in cpp: die("unique admin generation missing "+m)

    # A soft radio recovery must precede the existing hard reboot path.
    for m in ("P1_POOL_SOFT_RECOVERY_MS 60000UL","baseStationRecoverRadio()",
              "soft_radio_recovery_attempted","P1_POOL_STALL_REBOOT_MS 120000UL","board.reboot()"):
        if m not in sc+"\n"+cpp: die("staged recovery missing "+m)
    if sc.find("baseStationRecoverRadio()") > sc.find("board.reboot()"):
        die("soft recovery must appear before hard reboot")

    # CAD AUTO is disabled in low-power modes unless explicitly requested.
    if "(baseStationPowerState() == 0 && baseStationPressureLevel() >= 2)" not in h:
        die("power-aware CAD AUTO missing")

    # V2 remains exactly on the phase-1 radio contract.
    for m in ("869.618f","62.5f","_prefs.sf = 8","_prefs.cr = 5"):
        if m not in cpp: die("EU868 contract regression "+m)

    print("P1 Pro V2 phase-2 contracts OK")

if __name__=="__main__": main()
