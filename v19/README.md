# MeshOffGridNL V19 — Factory Clean Wi-Fi

V19 is a deliberately clean Wi-Fi recovery build for the LILYGO T-Deck / T-Deck Plus and Samsung Galaxy S21 hotspot tests.

## What V19 fixes

The V11–V18 installer preserved NVS. Deeper source review shows WadaMesh touch builds also route touch settings and Wi-Fi credentials through `SdNvsPrefs` to SPIFFS or SD, using the `meshcomod` namespace. That means stale Wi-Fi state can survive beyond normal driver reconnect logic, and an SD-backed profile can survive a chip reflash.

V18 also used `WiFi.disconnect(true, true)` on its first attempt. Upstream WadaMesh explicitly warns that an erase-AP path can wedge this radio in some states. V19 removes erase-AP from the runtime association path.

V19 therefore combines:

- **factory-clean installer mode**: full ESP32-S3 chip erase before writing the validated four firmware components;
- **first-boot Wi-Fi sanitiser**: after WadaMesh selects its active prefs backend (SPIFFS or SD), V19 clears saved SSID/password from that active backend, restores Wi-Fi intent, flushes file-backed prefs, and records a one-time marker only after the clear is verified;
- **plain Arduino/ESP-IDF association**: no BSSID lock, no channel lock, no forced PMF/SAE, no forced PHY/bandwidth and no erase-AP on the V19 connection path;
- **safe third-attempt radio restart**: if two normal attempts fail, V19 restarts STA without erasing credentials and retries the same standard path;
- **visible V19 diagnostics** in the Wi-Fi UI so a device running the new build is obvious.

The V16 scan arbitration, one reconnect owner, bounded retry/backoff and V15 crash protection remain active.

## Important

The V19 website installer intentionally uses a full-chip erase. Internal settings/state from older builds are removed. V19 also clears Wi-Fi SSID/password from the active WadaMesh file-backed preferences store on first boot, including SD-backed prefs if that backend is active. Other SD data is not intentionally erased.

Physical validation on the affected Galaxy S21 remains required before calling this RC stable.
