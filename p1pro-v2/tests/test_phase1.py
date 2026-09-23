#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(x): raise SystemExit("P1 Pro V2 phase-1 contract failure: "+x)

def main():
    if len(sys.argv)!=2: die("usage: test_phase1.py <patched MeshCore>")
    root=pathlib.Path(sys.argv[1])
    variant=(root/"variants/sensecap_solar/platformio.ini").read_text()
    cpp=(root/"examples/simple_repeater/MyMesh.cpp").read_text()
    h=(root/"examples/simple_repeater/MyMesh.h").read_text()
    cli=(root/"src/helpers/CommonCLI.cpp").read_text()
    sup=(root/"examples/simple_repeater/BaseStationSupervisor.cpp").read_text()

    for m in (
      "MESH_OFFGRIDNL_P1PRO_V2=1",
      "-D LORA_FREQ=869.618","-D LORA_BW=62.5","-D LORA_SF=8","-D LORA_CR=5","-D LORA_TX_POWER=22"
    ):
      if m not in variant: die("build marker missing "+m)

    # V2 must restore the exact V19 radio profile after loading persisted prefs.
    load=cpp.find("_cli.loadPrefs(_fs);")
    acl=cpp.find("acl.load(_fs, self_id);",load)
    block=cpp[load:acl]
    for m in ("_prefs.freq = 869.618f;","_prefs.bw = 62.5f;","_prefs.sf = 8;","_prefs.cr = 5;",
              "if (_prefs.tx_power_dbm > 22) _prefs.tx_power_dbm = 22;"):
      if m not in block: die("persisted radio lock missing "+m)

    # 48 remains ceiling; memory remains bounded.
    for m in ("MESH_OFFGRIDNL_P1PRO_FLOOD_MAX=48","MESH_OFFGRIDNL_P1PRO_UNSCOPED_MAX=6",
              "MESH_OFFGRIDNL_P1PRO_ADVERT_MAX=8"):
      if m not in variant: die("routing ceiling missing "+m)
    if "p1_base_station_pool(32)" not in cpp: die("32 packet pool changed")

    # Four adaptive pressure states and CAD AUTO at severe/critical only.
    for m in ("return 3; // CRITICAL","return 2; // SEVERE","return 1; // BUSY","return 0; // NORMAL"):
      if m not in cpp: die("pressure state missing "+m)
    if "baseStationPressureLevel() >= 2" not in h: die("CAD AUTO threshold missing")

    # CLI cannot silently move V2 off 868 or above the hardware ceiling.
    for m in ("P1 Pro V2 is EU868/V19 locked",
              "P1 Pro V2 frequency is locked to 869.618 MHz",
              "P1 Pro V2 TX range is -9..22 dBm",
              "_callbacks->applyTempRadioParams(869.618f, 62.5f, 8, 5"):
      if m not in cli: die("CLI lock missing "+m)

    # Preserve V1 safety layers.
    for m in ('"/prefs.tmp"','"/prefs.bak"'):
      if m not in cli: die("safe prefs regression "+m)
    if "P1_POOL_STALL_REBOOT_MS 120000UL" not in sup: die("hard-stall protection missing")

    print("P1 Pro V2 phase-1 contracts OK")

if __name__=="__main__": main()
