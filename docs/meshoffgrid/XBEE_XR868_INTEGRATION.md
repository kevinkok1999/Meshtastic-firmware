# Digi XBee XR 868 integration for T-Deck

Status: **design + driver scaffold**

Branch: `feature/xbee-xr868-transport`

## Goal

Add Digi XBee XR 868 as an **optional second long-range transport** next to the existing SX1262 LoRa radio.

The first rule of this integration is:

> Existing Meshtastic/LoRa behaviour must remain unchanged when XBee support is disabled.

The XBee path is therefore isolated behind the compile-time feature flag:

```text
MESHOFFGRID_ENABLE_XBEE_XR868
```

## Why this is useful

The T-Deck already has an SX1262 LoRa radio. XBee XR 868 adds a second independent 863–870 MHz radio/network path using DigiMesh.

That allows the future routing layer to make choices such as:

```text
message
   |
   +-- LoRa / Meshtastic
   +-- XBee XR 868 / DigiMesh
   +-- future transports
   |
store / retry / forward
```

XBee is **not** a LoRa amplifier and does not directly speak Meshtastic over RF. A route using XBee requires XBee-capable endpoints or gateways.

## Hardware facts relevant to the current T-Deck variant

Do **not** assign XBee UART pins by guessing.

The current T-Deck variant already uses:

| Function | GPIO |
|---|---:|
| GPS RX | 44 |
| GPS TX | 43 |
| LoRa/TFT/SD shared SPI SCK | 40 |
| LoRa/TFT/SD shared SPI MOSI | 41 |
| LoRa/TFT/SD shared SPI MISO | 38 |
| LoRa CS | 9 |
| LoRa RESET | 17 |
| LoRa DIO1 | 45 |
| LoRa BUSY | 13 |
| I2C SDA | 18 |
| I2C SCL | 8 |
| Keyboard backlight | 46 |
| Touch IRQ | 16 |

Because GPIO 43/44 are already allocated to GPS, the XBee implementation must not silently reuse the default UART pins.

Before the first hardware build we need one of these options:

1. confirm a safe pair of unused ESP32-S3 GPIOs exposed on the T-Deck hardware;
2. use an external UART bridge/expansion PCB;
3. deliberately disable/re-route another peripheral in a dedicated hardware variant.

The software driver therefore accepts RX/TX pins at runtime and does not hardcode them.

## XBee operating mode

Use **API mode** for the integration rather than Transparent mode.

Initial target:

- AP = 1 (API mode without escaping)
- AO = 0 (standard receive frames)
- Transmit Request = frame type `0x10`
- Extended Transmit Status = `0x8B`
- Receive Packet = `0x90`

The driver currently implements the framing required for those core packet paths.

## Source layout

```text
src/mesh/xbee/
├── XBeeApiCodec.h
├── XBeeApiCodec.cpp
├── XBeeXr868Link.h
└── XBeeXr868Link.cpp
```

### XBeeApiCodec

Pure frame encoding/validation helpers:

- builds API `0x10` transmit frames;
- computes Digi checksum;
- validates received API frames.

It does not know about Meshtastic messages or routing.

### XBeeXr868Link

ESP32 UART-facing driver:

- opens a chosen HardwareSerial port;
- parses the API byte stream;
- emits callbacks for:
  - `0x90` received RF packets;
  - `0x8B` transmit status;
  - `0x8A` modem status;
- sends unicast or broadcast data using `0x10`.

No routing policy belongs in this class.

## Planned integration boundary

Do not inject XBee-specific logic into the existing LoRa radio driver.

The intended upper-layer contract is:

```text
Application / message envelope
            |
        Route manager
       /      |      \
    LoRa     XBee    future
       \      |      /
        delivery state
            |
     store-and-forward
```

A later route manager should own:

- message IDs;
- TTL;
- duplicate suppression;
- retries;
- transport scoring;
- delivery acknowledgements;
- store-and-forward;
- RF coexistence scheduling.

## RF coexistence requirement

Both SX1262 and XBee XR 868 operate around 868 MHz.

The first prototype must therefore avoid simultaneous local transmit/receive assumptions. Before enabling both radios continuously on one handheld, add an RF scheduler that can coordinate radio activity and collect real measurements for desense/interference.

Do not treat two nearby 868 MHz radios as independent until this has been measured.

## Phase plan

### Phase 1 — driver scaffold

- [x] Isolated source directory
- [x] Compile-time feature flag
- [x] API `0x10` transmit frame encoder
- [x] API stream parser
- [x] Receive Packet `0x90` callback
- [x] Extended Transmit Status `0x8B` callback
- [x] Modem Status `0x8A` callback
- [ ] Native/unit tests for codec
- [ ] Confirm exact XR 868 payload limits from configured RF mode

### Phase 2 — hardware bring-up

- [ ] Select physical XBee form factor
- [ ] Confirm supply design
- [ ] Confirm safe UART pin mapping
- [ ] Configure AP=1 / AO=0
- [ ] Read module identity using API AT frames
- [ ] Send T-Deck A -> XBee A -> XBee B -> T-Deck B test payload
- [ ] Capture delivery status and error codes

### Phase 3 — MeshOffGrid message bridge

- [ ] Define transport-independent message envelope
- [ ] Add XBee source/destination mapping
- [ ] Duplicate suppression cache
- [ ] TTL
- [ ] local delivery acknowledgement
- [ ] queue undelivered messages

### Phase 4 — intelligent routing

- [ ] LoRa/XBee transport health
- [ ] route scoring
- [ ] retry/backoff
- [ ] store-carry-forward
- [ ] multipath policy
- [ ] metrics screen/logging

### Phase 5 — RF coexistence and field tests

- [ ] RSSI/link tests
- [ ] simultaneous-radio interference test
- [ ] antenna placement test
- [ ] power-consumption test
- [ ] open-field test
- [ ] built-up-area test
- [ ] regression test with XBee feature disabled

## Definition of done for the first prototype

The XBee prototype is considered successful when:

1. stock LoRa/Meshtastic still works with XBee disabled;
2. the T-Deck can exchange arbitrary binary payloads through two XR 868 modules;
3. transmit success/failure is returned to the ESP32;
4. incoming packets expose the 64-bit XBee source address;
5. no board pins are shared accidentally;
6. reconnect/reset does not corrupt the parser;
7. corrupted API frames are rejected by checksum;
8. the driver performs no blocking wait for RF delivery;
9. routing code remains separate from the UART/API driver.

## Safety rule for development

Do not increase RF power, bypass regional radio limits, or modify certified RF parameters outside permitted operation. Range work should focus on legal configuration, antennas, placement, routing, redundancy and protocol design.
