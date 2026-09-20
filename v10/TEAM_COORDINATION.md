# V10 Team Coordination

## DirectLink protocol specialist
Owns:
- pair session;
- frames;
- MessageId128;
- fragmentation;
- selective ACK;
- FEC.

## SX1262 specialist
Owns:
- MeshCore radio handoff;
- LoRa home state;
- GFSK mode;
- restoration/recovery.

## ESP32 Wi-Fi specialist
Owns:
- ESP-NOW LR;
- Wi-Fi LR;
- AP/STA role election;
- Wi-Fi mode restoration.

## BLE specialist
Owns:
- BLE host/controller capability;
- Coded PHY;
- coexistence constraints;
- memory reservation.

## RF scheduler specialist
Owns:
- SUBGHZ/RF24 locks;
- mode transition state machines;
- deadlines;
- cooldowns;
- rollback.

## Optimization specialist
Owns:
- scoring;
- probe economy;
- transition cost;
- retry ladder;
- battery budget.

## QA/CI specialist
Owns:
- host tests;
- one-runner policy;
- hardware matrix;
- immutable assets.

## Installer specialist
Acts only after firmware + two-device QA.

## Rules
1. No transport adapter selects the global route.
2. No adapter changes a shared radio without scheduler ownership.
3. Every new mode has a deterministic rollback to home state.
4. No V10 feature may make V9 LoRa/ESP-NOW boot dependent on it.
5. Measurement claims come from two-device field tests, not theory.
