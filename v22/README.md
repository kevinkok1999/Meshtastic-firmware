# MeshOffGridNL V22

V22 gives the T-Deck Plus one Wi-Fi station owner. It uses the normal ESP32-S3 802.11 b/g/n path, leaves PMF optional, avoids forced channel/BSSID/PHY settings, and separates association, DHCP and internet reachability. ESP-NOW must pause during association and use the AP channel afterward; LoRa remains the fallback.

The installer must expose a standalone verified download for iPhone, iPad, Android and desktop. Web Serial is only for flashing. The download path validates HTTP status, Content-Length and SHA-256 from a signed release manifest, with timeout and bounded retry.

Physical release gate: 20 cycles on Galaxy S21 2.4 GHz WPA2, one iPhone with Maximize Compatibility, one recent Galaxy, WPA2 router, transition-mode AP, and LoRa/BLE coexistence. Compilation cannot prove every phone model.
