# MeshOffGridNL V12 — Base Wi-Fi + Global Chat

V12 is intentionally a thin layer on top of V11 and the pinned WadaMesh beta_83 base.

## Design rule

Wi-Fi itself must behave like the base firmware. V12 does **not** replace the WadaMesh Wi-Fi state machine, association policy, saved-network model, scanning flow or reconnect behaviour.

When the T-Deck is connected to Wi-Fi, the existing V11 encrypted global-DM bridge is active in the same chat UI. When Wi-Fi is unavailable, normal LoRa/MeshCore messaging continues to work.

## V12 behaviour

- Keep the WadaMesh beta_83 Wi-Fi settings and saved-network behaviour.
- Keep the base scan and network-selection flow.
- Keep up to eight known/saved networks as provided by the base firmware.
- Fix the T-Deck Wi-Fi network list so all saved/scanned networks are reachable by vertical scrolling instead of only the first few visible rows.
- Preserve V11 worldwide direct messages while Wi-Fi is connected.
- Preserve V11 LoRa/internet de-duplication so one logical message appears once in the chat.
- Preserve the LoRa path as the normal compatibility/off-grid path.

## Global direct messages

V11 supplies the worldwide route and V12 leaves it intact:

- per-peer key material is derived from the existing device identities/shared secret;
- internet payloads use AES-256-GCM;
- the internet copy is used only when Wi-Fi is actually connected;
- the same direct-message chat is used; there is no separate "internet chat";
- LoRa remains available in parallel and duplicate copies are suppressed.

## What V12 deliberately does not do

The earlier V12 Wi-Fi-hardening experiment has been removed. V12 does not add an NL country override, custom supplicant reset, custom modem-sleep association policy or its own disconnect-reason state machine.

The ESP32-S3 remains 2.4 GHz Wi-Fi hardware; V12 does not create 5 GHz support.

## Radio policy

The V11 T-Deck radio profile is unchanged: 869.618 MHz, 62.5 kHz, SF8, 22 dBm hardware cap, DIO2 RF switch and boosted RX.
