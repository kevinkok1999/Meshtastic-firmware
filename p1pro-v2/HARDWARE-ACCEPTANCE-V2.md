# MeshOffGridNL P1 Pro V2 RC1 — hardware acceptance

V2 RC1 may be published online for controlled testing. It is **not Stable** until the physical gates below pass.

## A — one-click installer
- Open the P1 Pro installer in Chrome/Edge on desktop.
- Connect the P1/P1 Pro by USB data cable.
- Click **Aansluiten & installeren**.
- The normal V2 flow must use an application DFU update **without Factory Clean**.
- Existing identity and valid operator configuration must survive a V1 -> V2 update.
- If automatic DFU re-enumeration is not exposed by the browser, the UI may ask for RST twice and a second click.
- Factory Clean, manual DFU and UF2 are recovery options only.
- No automatic bootloader update.

## B — EU868/V19 compatibility
- V2 remains locked to 869.618 MHz / 62.5 kHz / SF8 / CR5.
- TX setting cannot exceed 22 dBm.
- T-Deck V19 -> P1 V2 receive succeeds.
- P1 V2 -> T-Deck V19 receive succeeds.
- T-Deck A -> P1 V2 -> T-Deck B succeeds in both directions.
- Turning P1 V2 off must not break direct T-Deck <-> T-Deck operation.

## C — routing / congestion
- scoped flood ceiling = 48 hops.
- unscoped flood ceiling = 6.
- advert ceiling = 8.
- one-byte path mode remains enabled for >32-hop capability.
- baseline airtime ceiling = 10%.
- BUSY/CONGESTED/SEVERE tighten to 5% / 2% / 1%.
- CAD engages automatically under pressure.
- after quiet operation, pressure state recovers with hysteresis.
- fixed packet pool remains 32.

## D — Solar Guardian
- normal / ECO / SURVIVAL / CRITICAL transitions are observable.
- background adverts are suppressed at low-power states without disabling the LoRa dataplane.
- runtime samples use hysteresis.
- <=3300 mV for three valid samples without external power enters the existing SenseCAP protected low-voltage shutdown path.
- LPCOMP/VBUS wake is verified on hardware.
- GPS remains non-essential to LoRa forwarding.

## E — self-healing
- transient queue pressure does not reboot the device.
- sustained zero-free-packet/no-progress condition first drains queued packets back to the fixed pool.
- if still stalled, the radio is reinitialised.
- full MCU reboot occurs only after staged recovery also fails.
- config writes survive interrupted power through temp/backup/rename rollback.

## F — trust
- a device still using the published upstream admin default gets a per-device random credential.
- an existing custom credential survives upgrade.
- `base credential` is accessible only through the physical USB Serial handler.
- the credential command is not reachable through MyMesh remote-admin command handling.

## Stable promotion
Only after A-F pass on real P1/P1 Pro hardware:
- change website label from **V2 RC1** to **V2 Stable**;
- promote the GitHub prerelease;
- make V2 the stable recommended P1 firmware.
