# V10 Capability Matrix

## Physical resources

### SUBGHZ — SX1262
Logical modes:
- LORA_MESHCORE_COMPAT
- GFSK_DIRECT

Constraint:
The same SX1262 cannot be in LoRa and GFSK packet mode simultaneously.

The DirectLink RF scheduler owns every modem transition.
No transport adapter may reconfigure SX1262 independently.

### RF24 — ESP32-S3 integrated radio
Logical modes:
- ESPNOW_LR
- WIFI_LR_DIRECT
- BLE_CODED

Constraint:
ESP-NOW and Wi-Fi share the Wi-Fi MAC/PHY.
BLE uses Bluetooth/Wi-Fi coexistence on the same 2.4-GHz RF hardware.
BLE Coded PHY occupies the radio longer than 1M/2M BLE and can reduce Wi-Fi performance.

Therefore V10 schedules measurement and delivery windows rather than attempting uncontrolled concurrency.

## Verified software baseline from successful V9 build

- PlatformIO Espressif32: 7.1.3
- Arduino ESP32 package: framework-arduinoespressif32 4.20017.260907
- Xtensa ESP32-S3 toolchain: 8.4.0+2021r2-patch5
- RadioLib resolved: 7.7.1
- Saitama upstream: 16598b4e6a7eabd6195d06d2a3abc1b3b448f368
- MeshCore: d92964352441e53b93e8667b802e04f6e072b39e

## Coding proof gates

Before integrating each bearer into chat:

### BLE Coded
Compile a minimal probe in the exact V10 toolchain that:
- retains BLE memory under Arduino;
- initializes the existing compatible BLE host stack;
- checks controller Coded PHY support;
- selects Coded PHY using the API spelling appropriate to the bundled ESP-IDF;
- confirms fallback when Coded PHY cannot be negotiated.

Do not assume the Arduino high-level BLE wrapper exposes every PHY control.

### Wi-Fi LR Direct
Compile a minimal proof that:
- sets WIFI_PROTOCOL_LR on the selected interface;
- establishes T-Deck A/B SoftAP/STA roles on one fixed negotiated channel;
- sends authenticated UDP probe/ACK frames;
- restores ESP-NOW LR without reboot.

### SX1262 GFSK
Compile a minimal proof using the exact RadioLib version that:
- pauses MeshCore radio ownership cleanly;
- switches SX1262 to GFSK;
- sends and receives a V10 probe;
- restores the exact LoRa configuration;
- restarts MeshCore receive state;
- proves no stuck BUSY/IRQ condition.

GFSK is not declared release-ready until repeated LoRa->GFSK->LoRa cycles pass on two physical T-Decks.
