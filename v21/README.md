# MeshOffGridNL V21 — Wi-Fi First

V21 pauses feature work and treats reliable Wi-Fi association on the LILYGO T-Deck Plus as the release blocker.

## Three phases

### Phase 1 — Golden Wi-Fi A/B isolation
Build two tiny Wi-Fi-only firmwares with the **same application code**:

- **Golden A (legacy stack):** PlatformIO espressif32 6.11.0 / Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7.
- **Golden B (modern stack):** current pioarduino stable / Arduino-ESP32 3.3.x / ESP-IDF 5.5.x.

Both builds deliberately contain:
- no WadaMesh;
- no MeshCore;
- no LoRa;
- no BLE / NimBLE;
- no MQTT;
- no Local AI;
- no UI scan worker;
- no BSSID/channel pinning;
- no PMF/SAE forcing;
- no Wi-Fi OFF/ON retry cycle;
- no driver erase during reconnect.

They register events before Wi-Fi starts, enter STA mode once, call `WiFi.begin()`, and treat `STA_CONNECTED` and `GOT_IP` as separate states.

For the first hardware experiment, temporarily configure the Samsung S21 hotspot as:

- SSID: `MeshOffGrid21`
- password: `MeshOffGrid21Test`
- band: 2.4 GHz
- security: WPA2-Personal if selectable
- hidden network: off

The test credentials are intentionally public and must only be used for this short diagnostic test.

### Phase 2 — V21 WiFiManager
Only after a Golden build connects reliably, replace V13-V19 association/retry orchestration in the full firmware with one event-driven owner.

Keep:
- chats;
- LoRa/MeshCore;
- UI;
- Local AI;
- storage.

Replace:
- repeated `disconnect()` before every join;
- third-attempt Wi-Fi OFF/ON cycle;
- BSSID/channel hints in the normal join path;
- forced security/PHY settings;
- overlapping reconnect owners;
- scan/connect overlap.

BLE is reintroduced only after Wi-Fi works with BLE absent.

### Phase 3 — full integration and production
Run an explicit hardware matrix:
- S21 2.4 GHz WPA2;
- S21 WPA2/WPA3 transition if available;
- ordinary router;
- hotspot off/on;
- T-Deck reboot;
- phone reboot;
- repeated reconnects;
- LoRa active;
- BLE off, then BLE on;
- scans only while idle;
- DHCP recovery.

V21 is not marked Stable until physical hardware passes.

## Diagnostic truth table

- `STA_DISCONNECTED` before `STA_CONNECTED`: association/auth/RF path.
- `STA_CONNECTED` but no `GOT_IP`: association succeeded; investigate DHCP/netif.
- `GOT_IP`: link + DHCP succeeded.

The firmware prints the numeric ESP-IDF disconnect reason so we can stop guessing and identify the failing stage.
