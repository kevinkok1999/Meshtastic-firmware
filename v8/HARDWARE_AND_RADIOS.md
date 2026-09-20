# V8 Hardware and Radio Contract

## 1. Stock T-Deck Plus

Keep the existing working hardware path unchanged:
- ESP32-S3
- onboard SX1262
- ESP-NOW LR on ESP32-S3 Wi-Fi
- existing display, keyboard, trackball, touch, SD and GPS behavior

The T-Deck Plus Grove pins are used by GPS and must not be silently reused for V8 expansion.

## 2. Digi XBee XR 868

Target family:
- Digi XBee XR 868
- EU 863-870 MHz
- DigiMesh
- external module

Preferred software interface:
- UART API mode first
- SPI only if a later expansion-board design justifies it

Bring-up sequence:
1. open configured UART
2. identify module
3. read firmware/version/address
4. read effective NP/MTU
5. verify regional configuration
6. mark transport available
7. register delivery-status callbacks

No hard-coded RX/TX pins until the exact expansion connector/PCB is validated.

## 3. Semtech LR2021

Target:
- LR2021 EU868-capable external radio/reference module
- SPI interface
- RadioLib-supported driver path

Bring-up sequence:
1. acquire SPI bus safely
2. initialize LR2021
3. verify chip/driver state
4. apply EU868-compatible profile
5. release bus cleanly on failure
6. mark transport available only after a receive/transmit self-check path exists

Do not replace the onboard SX1262 in the first V8 build. LR2021 is an optional second radio.

## 4. Expansion-board rule

The preferred end state is a small V8 expansion board that provides:
- protected 3.3 V rail sized for the external radios
- XBee socket/connector
- LR2021 module connector
- separate antennas/connectors
- level-safe UART/SPI routing
- optional enable/power-gate lines
- mechanically safe mounting

Firmware must not depend on this board existing.

## 5. Power

V8 must record radio activity and avoid powering optional modules unnecessarily.

Hardware profiles may expose:
- xbeePowerEnable
- lr2021PowerEnable
- radioWakeDelayMs

If power gating is not physically present, the setting is ignored rather than simulated.

## 6. RF coexistence

Never assume three nearby 868 MHz radios can transmit/receive independently without desense.

Required bench tests:
- SX1262 TX while XBee RX
- XBee TX while SX1262 RX
- LR2021 TX while SX1262 RX
- SX1262 TX while LR2021 RX
- XBee TX while LR2021 RX
- antenna separation/orientation tests
- idle-current and active-current measurements

The first production V8 policy serializes local sub-GHz transmissions.

## 7. Antennas

Each external transmitter must use an antenna/path appropriate to that module and regional configuration.

Do not share one antenna between radios unless a purpose-designed RF switch/diplexer network is later engineered and validated.

## 8. Hardware stop conditions

Coding/hardware bring-up stops instead of guessing if:
- no safe exposed GPIO/UART/SPI mapping is confirmed;
- supply stability is not adequate;
- a proposed pin conflicts with GPS/display/SD/LoRa/keyboard/touch;
- RF coexistence produces repeatable corruption or resets;
- a regional configuration cannot be kept compliant.
