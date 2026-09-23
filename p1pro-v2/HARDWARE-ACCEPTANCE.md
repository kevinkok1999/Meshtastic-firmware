# MeshOffGridNL P1 Pro V2 — Hardware Acceptance

Release line: **P1 Pro V2 RC1**
Hardware: **Seeed SenseCAP Solar Node P1 / P1 Pro**
Compatibility target: **MeshOffGridNL T-Deck V19**
Band: **EU868 only**

RC1 can be published for controlled hardware validation. It MUST remain a prerelease until the physical gates below pass.

## Gate A — one-click installation

- Open the MeshOffGridNL P1 installer in desktop Chrome/Edge.
- Connect the P1/P1 Pro over a USB data cable.
- One primary **Install P1 Pro V2** action downloads and verifies all assets before changing flash.
- The installer attempts application -> DFU transition automatically.
- Factory Clean is applied automatically for the RC1 clean-install path.
- The installer attempts to reacquire the authorized serial device after re-enumeration.
- If browser permissions prevent automatic reacquisition, only a single reselect-device action is required.
- V2 DFU ZIP installs successfully.
- UF2 fallback remains independently usable.
- No automatic bootloader upgrade occurs.

## Gate B — V19 radio compatibility

The radio is locked to:

- 869.618 MHz
- 62.5 kHz bandwidth
- SF8
- CR5
- TX ceiling 22 dBm

Verify:
- T-Deck V19 hears/discovers P1 Pro V2.
- P1 Pro V2 receives V19 traffic.
- T-Deck A -> P1 V2 -> T-Deck B succeeds.
- Reverse direction succeeds.
- T-Deck A <-> B still functions when P1 V2 is powered off.

## Gate C — Adaptive Mesh

- NORMAL policy is capped at 10% airtime.
- BUSY -> 5%, CONGESTED -> 2%, SEVERE -> 1%.
- Escalation is immediate.
- Recovery is hysteretic (30 seconds quiet per level).
- CAD is adaptive and does not remain forced on after pressure clears.
- The fixed 32-packet pool remains bounded.
- 48 hops remains a ceiling, not a normal path target.

## Gate D — Solar Guardian

- External USB power prevents low-voltage shutdown.
- ECO begins below 3600 mV and recovers at/above 3700 mV.
- CRITICAL begins below 3450 mV and recovers at/above 3550 mV.
- Two valid readings below 3300 mV trigger protective LOW_VOLTAGE shutdown.
- LPCOMP/VBUS recovery can wake the node after power returns.
- ECO/CRITICAL shed background traffic but never disable core LoRa forwarding.

## Gate E — Trust and recovery

- A fresh node never remains on upstream admin password `password`.
- Existing operator-set credentials survive an upgrade.
- No secret is emitted in health logs.
- At a hard packet-pool stall, radio recovery is attempted before full MCU reboot.
- If the soft recovery does not restore progress, the hard reboot path remains available.
- Atomic prefs backup/rollback survives interrupted writes.
- No reset loop during soak testing.

## Stable promotion

Only after Gates A-E and a long-running soak test pass:
- change website label from **V2 RC1** to **V2 Stable**
- promote GitHub prerelease
- keep V1 RC1 available as explicit rollback/fallback
