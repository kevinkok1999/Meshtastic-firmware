# MeshOffGridNL V18 — Clean Wi-Fi Boot

V18 is a Samsung Galaxy S21 hotspot reliability release built on V17.

It changes strategy after comparing the Wi-Fi recovery patterns used by projects such as Meshtastic, ESPHome and WLED: instead of adding more security overrides, the first association attempt starts from a clean ESP32 Wi-Fi station state and lets Arduino/ESP-IDF perform its normal WPA2/WPA3 negotiation.

## V18 staged connection

1. **Clean station boot**
   - app credentials remain stored by MeshOffGridNL;
   - stale driver AP credentials are erased;
   - Wi-Fi is taken fully to NULL/OFF;
   - the driver is given time to settle;
   - STA mode is re-created;
   - the normal Arduino `WiFi.begin(ssid, password)` path is used, which also restores normal DHCP handling.

2. **Normal retry**
   - if the first association fails, the driver AP configuration is cleared again without another full radio restart;
   - a second plain SSID/password `WiFi.begin()` is attempted.

3. **V17 security fallback**
   - the third foreground attempt retains V17's direct ESP-IDF fallback with PMF-capable/optional and SAE HnP+H2E.

V16 scan arbitration, one reconnect owner, bounded retries and the V15 crash fix remain active. No `WiFi.setSleep(false)` association override is reintroduced.

This is intentionally less clever on attempts 1 and 2: the standard Espressif/Arduino station stack owns authentication, DHCP and security negotiation.

Physical validation on the affected Galaxy S21 is still required before this RC can be called stable.
