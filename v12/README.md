# MeshOffGridNL V12 — Wi-Fi Hardening

V12 is an incremental hardening release on top of V11. It keeps the WadaMesh beta_83 / MeshCore base, LoRa configuration, encrypted global direct-message bridge, no-SD essential operation and four-component NVS-preserving installer unchanged.

## Why V12 exists

A real T-Deck Plus could scan nearby Wi-Fi networks but remained on `Connecting...` when joining both a router and a 2.4 GHz phone hotspot. Source review found several association-path gaps worth fixing independently of the user's router:

- upstream WadaMesh does not explicitly set the ESP32-S3 Wi-Fi country, so a disconnected station starts from the world-safe channel policy rather than an explicit Netherlands/EU 2.4 GHz policy;
- changing credentials uses a light disconnect in the apply path, while the later retry path already uses a stronger supplicant reset;
- modem sleep is deliberately disabled before the first association but was not explicitly disabled again before every later association attempt;
- the firmware records ESP-IDF disconnect reason codes, but failure diagnosis is not prominent enough on-device.

## V12 changes

- Set station country to `NL` after STA initialization, with 802.11d enabled.
- Force modem sleep OFF before each association attempt, then restore modem sleep after a successful connection.
- Clear stale supplicant/AP state when applying a changed network.
- Preserve T-Deck automatic reconnect.
- Surface useful on-device failure reasons without revealing passwords:
  - handshake/password timeout;
  - authentication/security mismatch;
  - network not found / wrong band or channel;
  - incompatible security;
  - beacon/signal timeout;
  - generic connection failure.
- Keep all V11 LoRa/radio settings byte-for-byte in policy: 869.618 MHz, 62.5 kHz, SF8, 22 dBm hardware cap, DIO2 RF switch and boosted RX.

V12 does **not** add 5 GHz support. ESP32-S3 Wi-Fi remains 2.4 GHz hardware. The goal is robust 2.4 GHz association with routers and phone hotspots.
