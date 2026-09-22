# V20 Architecture — Mesh AI Core

## 1. Baseline preserved from V19

V20 is applied after V11, V12, V13, V15, V16, V17, V18 and V19.

It must preserve:
- V16 scan arbitration;
- exactly one reconnect owner;
- bounded retry/backoff;
- V15 crash protection;
- V19 one-time active prefs sanitizer;
- V19 non-erasing normal Wi-Fi association path;
- V19 safe third-attempt STA restart;
- V19 visible diagnostics;
- the current WadaMesh/MeshCore radio and UI stack.

V20 does not rewrite the radio protocol or Wi-Fi driver.

## 2. Module layout

All new source is isolated under:

```
v20/overlay/src/mesh-ai/
  MeshAiCore.h/.cpp
  MeshAiContext.h/.cpp
  MeshAiIntent.h/.cpp
  MeshAiPolicy.h/.cpp
  MeshAiActions.h/.cpp
  MeshAiRouteAdvisor.h/.cpp
  MeshAiStorage.h/.cpp
  MeshAiDiag.h/.cpp
  MeshAiUiBridge.h/.cpp
```

The V20 patch should make the minimum required changes to:
- `platformio.ini`
- `src/main.cpp`
- `src/ui-touch/UITask.h`
- `src/ui-touch/UITask.cpp`

## 3. Runtime architecture

```
keyboard/touch
     |
     v
 MeshAiUiBridge
     |
     v
 MeshAiCore
  |   |   |
  |   |   +--> MeshAiDiag
  |   +------> MeshAiRouteAdvisor
  +----------> MeshAiIntent -> MeshAiPolicy
                         |
                         v
                   MeshAiActions
                         |
                existing public APIs
             /      |       |       \
          UI      Wi-Fi    Mesh     GPS
```

The AI layer is not allowed to call low-level `esp_wifi_*` or RadioLib driver functions directly.

## 4. Context model

`MeshAiContext` produces a read-only snapshot with bounded fields:

- battery percentage / charging if available;
- GPS fix + age + coordinates only when needed;
- Wi-Fi radio state;
- SSID-connected state without exposing stored passwords;
- current V16/V19 join phase and last disconnect reason;
- IP-ready state;
- mesh/contact count;
- last-seen timestamps;
- recent message delivery state;
- per-peer recent RSSI, SNR, hop/path length and ACK success;
- SD mounted state;
- free internal heap and free PSRAM;
- firmware version and feature flags.

Sensitive keys/passwords are never copied into the AI context.

## 5. Intent engine

V20 uses a hybrid local parser.

### Layer A — deterministic grammar
Fast, tiny, predictable and default.

Intent classes:
- STATUS_DEVICE
- STATUS_WIFI
- STATUS_RADIO
- STATUS_REACHABLE
- STATUS_ROUTE
- SHOW_LOCATION
- SEND_TEXT
- SEND_LOCATION
- OPEN_SCREEN
- SET_LOCAL_OPTION
- HELP

Entity extraction:
- contact/thread name;
- message body;
- screen name;
- option/value.

### Layer B — optional tiny classifier
Compile-time flag:
`MESH_AI_TINY_ML=0` by default.

Later, a small quantized classifier may map a short command to the intent classes. It never generates free-form text and never bypasses policy validation.

Fallback is always the deterministic grammar.

## 6. Policy engine

`MeshAiPolicy` validates every parsed action.

Rules:
- read-only intents may execute immediately;
- SEND_TEXT requires an explicit recipient and non-empty user-authored message;
- SEND_LOCATION requires an explicit recipient/group;
- ambiguous recipient => show choices, do not transmit;
- low-confidence ML result => fall back to suggestions;
- no command may expose Wi-Fi passwords, private keys or channel keys;
- no command can silently alter LoRa frequency, power, region, channel keys or Wi-Fi security parameters;
- network/radio configuration changes remain in normal settings UI.

## 7. Route Advisor

V20 Route Advisor is advisory/selection logic, not a new radio protocol.

For each peer/transport it keeps bounded recent statistics:
- ACK success ratio;
- EWMA RSSI;
- EWMA SNR;
- median hop/path length;
- last success age;
- optional observed latency;
- Wi-Fi/IP availability.

Example normalized score:
```
score =
  35% recent delivery success +
  20% freshness +
  15% SNR quality +
  10% RSSI quality +
  10% low hop count +
  10% battery/cost preference
```

Weights are constants in V20 and can be tuned later.

Important: the advisor only chooses among transports/actions that actually exist in the current firmware. It must not claim an internet messaging route exists merely because Wi-Fi has an IP address.

## 8. Local learning

V20 learns only small operational statistics.

Stored per known peer:
- last seen;
- last successful route;
- rolling delivery success;
- rolling RSSI/SNR;
- rolling hop count.

Preferred storage:
1. microSD;
2. SPIFFS fallback;
3. RAM-only if neither is writable.

No cloud sync is required.

## 9. UI

Do not add a fifth bottom tab in V20.

Keep:
Home / Chats / Contacts / Settings.

Add a **Mesh AI** entry/card on Home.

Mesh AI screen:
- local-only badge;
- single-line/multi-line command input;
- result card;
- suggested commands;
- network health chips: Radio / Wi-Fi / GPS / Battery;
- optional action confirmation card when a target is ambiguous;
- compact local history, bounded to the most recent entries.

Keyboard is the primary input. Touch and trackball remain supported.

## 10. Scheduling

V20 must not block mesh/radio processing.

Design:
- one bounded command queue;
- one low-priority worker;
- deterministic parser runs synchronously only for short bounded input;
- route statistics updates are O(1);
- storage flushes are deferred/rate-limited;
- optional ML inference runs only on user request, never continuously;
- watchdog-safe time budget with yield points for any inference path.

## 11. Memory budget

T-Deck Plus has limited internal SRAM even though PSRAM is available.

Release target:
- permanent internal-heap cost: <= 64 KB;
- permanent AI PSRAM cost: <= 256 KB without optional ML;
- local history: bounded;
- route table: bounded by existing contact/thread limits but allocated sparsely;
- optional model: loaded from SD/flash into PSRAM only while needed when feasible.

The build must print binary size. Runtime diagnostics must expose free internal heap and PSRAM.

## 12. Optional voice path

Compile-time experimental flag:
`MESH_AI_VOICE=0` by default.

The T-Deck Plus microphone hardware makes future local speech commands possible, but V20 Stable does not depend on voice. Voice must feed the same intent/policy pipeline; it never gets a privileged action path.

## 13. Internet-ready abstraction

V20 introduces a transport-neutral action boundary but does not invent a cloud backend.

Concept:
```
IMessageTransport
  - Radio/Mesh adapter: available now
  - Internet adapter: only reports available if a real backend/session exists
```

This prepares a future release for one-inbox internet + radio messaging without corrupting V20's claims.

## 14. Failure behavior

If any AI component fails:
- chat remains usable;
- LoRa remains usable;
- Wi-Fi remains usable;
- V19 reconnect logic remains authoritative;
- AI UI shows unavailable/restartable state;
- no boot loop may be caused by corrupt AI state.

A boot-safe flag allows AI to be disabled for recovery.

## 15. Version/build flags

Required:
- `MESH_OFFGRIDNL_V20=1`
- `MESH_AI_ENABLED=1`

Optional:
- `MESH_AI_TINY_ML=0`
- `MESH_AI_VOICE=0`

V20 must keep all earlier V11-V19 feature markers present.
