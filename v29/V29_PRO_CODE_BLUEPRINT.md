# V29 PRO — CODE BLUEPRINT / BUILD DRAWING 01
**2026-10-20 · DETAILED PRE-CODING DESIGN · NOT YET APPROVED FOR CODING**
Target branch: v29-emergency-fabric. Exactly one V29 PRO release. This document is a blueprint, not proof of working firmware.

## A. Baseline: actual source attachment points
Overlay application: v29/apply_v29.py, applied AFTER V11,V12,V13,V15,V16,V17,V18,V19,V26,V27,V28. The script edits platformio.ini, src/MyMesh.h/.cpp, src/main.cpp, src/ui-touch/UITask.cpp and copies v29/overlay/**.
Actual V29 overlay APIs: V29EmergencyFabric::begin(MyMesh*), loop(), sendStructured(), sendCheckIn(), sendHelpRequest(), sendCheckInToEmergencyContacts(), sendHelpToEmergencyContacts(), onRawFrame(), takeEvent(), setEmergencyMode(), setPowerMode(), memoryStats(). V29EmergencyPortal::begin(), start(), stop(), loop(), active(), ssid(), password(), ipText(), remainingSeconds().
MyMesh::v29SendEmergencyRaw() uses createRawData() and sendFlood(). Receive hook MyMesh::onRawDataRecv() forwards raw frames to V29EmergencyFabric::onRawFrame() first.
Firmware env: LilyGo_TDeck_companion_radio_touch. Existing contract assumptions: 184-byte MAX_PACKET_PAYLOAD; emergency HEADER_LEN=82, TAG_LEN=16, SIGNATURE_SIZE=64 => BODY_MAX=22; protocol v2; TTL=192h; MAX_QUEUE_RECORDS=512; SEEN_CAP=256; EVENT_CAP=8; critical per origin max 8; portal session max 15min, idle stop 5min, action rate 3s.
**Design consequence:** The existing 22-byte structured emergency body is NOT a general-purpose chat payload. Do not pretend long chat is already supported by this fabric. Keep native V28 chat and introduce explicit bounded fragmentation only after interoperability review.

## B. Architectural drawing (ownership)
UI (LVGL) -> typed UI intents -> AppCoordinator (PROPOSED) -> Native V28 Chat | V29 Emergency Fabric | Portal | GPS | Diagnostics.
Transport owner: MyMesh existing RF TX/RX; no parallel SX1262 writer. Global internet relay is optional and must share logical IDs with native chat.
Storage owner: existing V29 SPIFFS snapshots for emergency queue; optional microSD for maps/history only. Proposed settings repository MUST NOT silently overwrite snapshots.
Wi-Fi owner: existing STA state machine or V29 portal AP, never both manipulating WiFi mode concurrently. Maintain portal/main interlock.
Security boundary: local browser is untrusted; no admin/identity/channel keys, POST-only actions, scoped session token, bounded request size and rate limits.
RF metrics read from supported driver callbacks only; no SDR, VSWR, 5GHz or RF power bypass.

## C. Implementation work packages and precise file plans
C01 Audit-only baseline: inspect upstream + v28-master four missing commits; record diff; inspect all 44 workflow triggers. Files: docs/V29_*; NO firmware modifications.
C02 Core event/contracts: introduce v29/overlay/src/helpers/esp32/V29AppCoordinator.h/.cpp only after checking existing event architecture; typed commands, bounded queues, no LVGL blocking. Integrate via v29/apply_v29.py and main.cpp anchors with replace_once preconditions.
C03 Security: extend V29EmergencyFabric.h/.cpp only after reviewing existing normalizedAuthData(), verifyWire(), replay/dedup, key derivation, nonce lifecycle; test malicious/expired frames. No bespoke crypto.
C04 RF/transport: preserve MyMesh::v29SendEmergencyRaw and onRawDataRecv dispatch; add single airtime budget and send arbitration only if missing. Test P1 V8 and V28 message paths.
C05 Portal: V29EmergencyPortal.h/.cpp; review token scope, request size/timeouts, CSRF and AP isolation; verify WiFi STA restoration and POST semantics.
C06 Message UI: src/ui-touch/UITask.cpp via apply_v29.py; native chat remains canonical; emergency actions never imply official 112 dispatch; focus/back/empty/error flows.
C07 Tropical UI: add compressed 320x240 asset manifest under v29/overlay/src/ui-touch/assets/ ONLY after measured flash/PSRAM budget; fallback solid theme and contrast layer. No arbitrary large wallpaper blobs.
C08 GPS/coverage: capability-gated adapter with fix age/accuracy and explicit opt-in, SD optional. Inspect existing GPS hooks before choosing new paths.
C09 Device health/power: extend V29EmergencyFabric::memoryStats and mode scheduler; bound loops and snapshots; record heap high-water, brownout, SD absence.
C10 Extensions: capability registry OFF by default for NFC/IR/external hardware until wiring, pins, driver and electrical compatibility are proven.
C11 Tests: v29/tests/ behavioral model + negative packet corpus; avoid marker-only tests; cross-check V28/P1 regression.
C12 Release: update v29_validation.yml only after full runner audit, pin toolchain, produce SHA256 manifest, size and build logs, installer verification.

## D. State machines
Message: CREATED -> VALIDATED -> QUEUED -> RF_ATTEMPTED -> (ACK_RECEIVED | RETRY_PENDING | EXPIRED | FAILED); relay acceptance is distinct from peer ACK. Every state transition persisted or recoverable, no phantom delivered badges.
WiFi: OFF -> ASSOCIATING -> IP_READY -> INTERNET_VERIFIED -> RELAY_AUTHENTICATED, with DEGRADED and AP_PORTAL_OWNED; portal ownership suspends STA reconnect and resumes deterministically.
Portal: STOPPED -> AP_STARTING -> ACTIVE -> IDLE_TIMEOUT/SESSION_TIMEOUT -> STOPPING -> STOPPED; every state-changing action is authenticated POST.
Emergency queue: ACCEPT -> VALIDATE -> DEDUP -> QUOTA -> PERSIST -> SCHEDULE -> TX -> RETRY/EXPIRE; P0 priority still subject to legal airtime.
UI: HOME -> CHAT/CONTACTS/EMERGENCY/TOOLS/SETTINGS; all screens have BACK, OFFLINE, EMPTY, ERROR and INPUT_FOCUS behavior.

## E. Numeric constraints — verified code constants vs provisional budgets
**Verified source constants:** 184-byte RF raw payload, 22-byte V29 emergency body, 512 maximum emergency records, 256 seen IDs, 8 event slots, 192h TTL, portal 15min max/5min idle/3s action limit.
**Provisional, NOT validated:** 320x240 wallpaper asset target <=150KiB flash; render frame target <=50ms at normal activity; UI event queue <=32 events; no extra heap allocation in radio callback; all tasks keep >25% measured stack headroom. These are engineering proposals and MUST be revised after baseline measurements.
**Safety invariants:** cache eviction before durable queue, never consume recovery partition for images, bounded input sizes, zero core dependency on SD/WiFi/phone, no false delivery receipts.

## F. Acceptance and negative tests (IDs)
BLD-01 clean upstream SHA plus V11→V29 overlay, deterministic compile; BLD-02 size and SHA256 artifact.
RF-01 V28 native RF messaging unchanged; RF-02 P1 Pro V8 interoperability; RF-03 loss/retry/duplicate with airtime limits.
MSG-01 same logical message from two transports appears once; MSG-02 forged/replayed/expired envelope rejected; MSG-03 queue full/power cut handled.
SEC-01 invalid AEAD/signature denied; SEC-02 token replay and GET actions denied; SEC-03 portal never exposes keys.
NET-01 LoRa remains functional with internet/BLE/SD absent; NET-02 portal AP start/stop restores STA; NET-03 Android/iPhone 2.4GHz hotspot.
UI-01 touch/trackball/keyboard paths; UI-02 every CTA has back/error/offline state; UI-03 emergency warnings accurate; UI-04 Tropical wallpaper readability.
SYS-01 SD removal; SYS-02 brownout during snapshot; SYS-03 low heap; SYS-04 extended soak and power consumption.
**Pass definition:** attach command, exact source SHA, device model, expected vs actual result, timestamp and log/artifact. A symbol presence check is NOT a behavioral PASS.

## G. Dependency graph / safe coding order
Audit reconciliation -> resource and pin map -> protocol/security review -> C02 core contracts -> C03/C04/C05 transport and security -> C06/C07 UI -> C08/C09 device services -> C10 optional extensions -> C11 regression -> C12 release.
No branch merge until four missing v28-master commits classified. No code modification until source-level feasibility confirmed and G1/G2 reviewed.

## H. Five specialist signoff cards
S1 Embedded: PENDING — memory/partition/pin measurement.
S2 RF: PENDING — regulatory and P1 test.
S3 Security: PENDING — threat model, nonce/replay/portal review.
S4 UX: PENDING — screen-by-screen wireframes and assets.
S5 QA: PENDING — automated/hardware test plan and CI inventory.
**Gate:** PC1 incomplete; PC2 blueprint DRAFT; PC3 NOT READY. Coding authorization: NO, until blockers resolved. This is a high-detail build drawing that identifies exactly what remains.
