# V21 Phase 1 — Physical Wi-Fi Test Gate

This gate must be passed before the full V21 firmware is integrated.

## Phone setup

Use the Samsung Galaxy S21 hotspot as the controlled test AP:

- Band: 2.4 GHz
- Security: WPA2-Personal if selectable
- Hidden network: disabled
- SSID: `MeshOffGrid21`
- Password: `MeshOffGrid21Test`

These credentials are intentionally temporary and public for the diagnostic build only.

## Test order

1. Flash Golden A (legacy stack).
2. Power-cycle the T-Deck.
3. Capture at least 60 seconds of USB serial output at 115200 baud.
4. Turn the hotspot off, wait 15 seconds, turn it back on, capture reconnect.
5. Reboot the T-Deck while the hotspot is already active.
6. Repeat the same sequence with Golden B (modern stack).
7. Repeat both builds against one normal 2.4 GHz router as a control.

Do not enable Bluetooth or LoRa for Phase 1.

## PASS criteria

A build passes the S21 test only if:

- it reaches `STA_CONNECTED`;
- it then reaches `GOT_IP`;
- it reports a non-zero IPv4 address;
- after hotspot off/on it reconnects without rebooting;
- after T-Deck reboot it reconnects;
- no watchdog reset, panic or boot loop occurs.

## Interpretation

### Disconnect before STA_CONNECTED
Association/authentication/RF path failed. The numeric reason code is the primary evidence.

### STA_CONNECTED but no GOT_IP
Wi-Fi association succeeded. Investigate DHCP / esp-netif / Android tethering rather than WPA/security.

### GOT_IP
Association and DHCP succeeded.

## Decision matrix

- A PASS + B PASS: old framework is not the root cause; full-firmware Wi-Fi orchestration is the leading suspect.
- A FAIL + B PASS: modern Arduino/IDF stack becomes the preferred V21 base.
- A PASS + B FAIL: modern-stack regression or board/toolchain mismatch; do not migrate.
- A FAIL + B FAIL on S21 but PASS on normal router: hotspot compatibility/coexistence/security is the leading branch.
- A FAIL + B FAIL on both S21 and router: inspect hardware/board/radio initialization before full integration.

## Required evidence to retain

For every run retain:
- stack name;
- Arduino version;
- ESP-IDF version;
- sequence of Wi-Fi events;
- numeric disconnect reason;
- IP address if obtained;
- RSSI and channel after success;
- whether reconnect succeeded after hotspot off/on.

The numeric reason code is more important than the human-readable guess.
