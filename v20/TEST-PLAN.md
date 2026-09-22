# V20 Test Plan

## Contract gate

V20 contract tests must first run every existing V11-V19 contract test.

New V20 checks:
- V20 build flag present;
- V19 build flag still present;
- V19 sanitizer markers still present;
- V19 association block still contains no erase-AP or forced PMF/SAE/PHY/BSSID/channel overrides;
- AI code contains no direct low-level RadioLib or `esp_wifi_*` mutation path;
- AI code contains no password/private-key logging;
- bounded command length and bounded history;
- send intents require explicit target and payload/location action;
- optional ML and voice default off.

## Parser tests

Minimum fixtures:
- Dutch status queries;
- English status queries;
- German status queries for basic device/network commands;
- contact extraction with spaces;
- quoted message body;
- unknown recipient;
- ambiguous recipient;
- empty message;
- over-length command;
- malformed UTF-8 handled safely.

## Policy tests

Must prove:
- read-only query can execute;
- ambiguous SEND_TEXT cannot transmit;
- SEND_LOCATION without target cannot transmit;
- private keys/passwords are never returned;
- AI cannot alter LoRa PHY settings;
- AI cannot alter Wi-Fi security settings;
- AI cannot bypass normal chat/message limits.

## Route Advisor tests

Synthetic peer histories:
- fresh direct ACKs outrank stale weak paths;
- failed path decays;
- missing data produces "unknown", not a fabricated score;
- Wi-Fi with IP but no internet messaging backend is not advertised as an internet send route;
- route statistics survive SD restart when SD is available;
- corrupt stats file fails back to empty state.

## UI tests

Compile/static checks:
- Home/Chats/Contacts/Settings enum remains unchanged;
- Mesh AI is opened from Home rather than changing bottom-tab count;
- input length capped;
- action/result widgets are destroyed cleanly;
- no blocking loop in LVGL callbacks.

## Runtime diagnostics

On physical T-Deck:
- record boot free internal heap and PSRAM;
- open/close Mesh AI 50 times;
- run 100 local status commands;
- keep LoRa receive active during commands;
- run Wi-Fi scan/connect while Mesh AI is open;
- verify no watchdog reset;
- verify no UI freeze;
- verify no missed normal chat behavior caused by AI.

## Wi-Fi regression

Mandatory on the Samsung Galaxy S21 target:
- fresh V20 clean install;
- 2.4 GHz hotspot visible;
- association;
- DHCP/IP;
- disconnect/reconnect;
- hotspot off/on;
- reboot T-Deck;
- reboot phone;
- third-attempt safe STA restart path.

V20 cannot be Stable if V19 Wi-Fi behavior regresses.

## Radio regression

- T-Deck to known compatible peer;
- direct message;
- reply;
- ACK/delivery state;
- RSSI/SNR/path metadata still populated;
- AI status reads metadata without modifying radio configuration.

## Build/release workflow

One GitHub runner only:
1. checkout;
2. fetch pinned WadaMesh beta_83 commit;
3. apply V11 -> V20 sequentially;
4. run all contract/parser/policy tests;
5. one PlatformIO T-Deck build;
6. size/manifest checks;
7. package firmware + corresponding source;
8. prerelease only;
9. website integration only after build artifacts/hashes exist.

## Release status

Initial release name:
**MeshOffGridNL V20 Local AI RC1**

Stable requires:
- CI green;
- physical T-Deck boot;
- physical Galaxy S21 Wi-Fi validation;
- radio message validation;
- AI parser/policy validation on device;
- no crash/watchdog during stress test.
