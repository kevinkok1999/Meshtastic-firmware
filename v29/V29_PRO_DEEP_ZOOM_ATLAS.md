# V29 PRO — DEEP ZOOM BLUEPRINT / ENGINEERING ATLAS
2026-10-20 · Design revision DZ-01 · Pre-coding only · no build or hardware certification claimed.
Hierarchy: System → subsystem → component → API/data/state → failure/abuse case → acceptance proof. Cross-reference V29_PRO_CODE_BLUEPRINT.md and V29_PRO_RECONCILIATION_GATE.md. **One V29 PRO release.**

## Z00 System integration
Goal: RF-first offline messaging with existing V28 chat, optional internet relay, independent V29 emergency fabric, local phone portal, GPS, maps, diagnostic UX, safe recovery. Primary UX remains Dutch.
Owners: AppCoordinator (proposed, not present), MyMesh (actual RF owner), V29EmergencyFabric (actual), V29EmergencyPortal (actual), LVGL UITask (actual).
Dependency graph: board/partition → radio/crypto/storage → message coordinator → transports → UI → diagnostics/extensions → release.
Global invariants: one radio TX owner; one WiFi mode owner; no blocking UI; one canonical ordinary chat ID; emergency actions never impersonate 112; secrets never cross portal; bounded buffers and retries; no assumption of Internet.
Traceability: every new code line belongs to requirement RQ-xxx and at least one automated/hardware test; no unlabeled critical paths.

## Z01 Physical device / power / electrical (S1)
Actual target: LILYGO T-Deck Plus ESP32-S3 and SX1262; target build env LilyGo_TDeck_companion_radio_touch. Confirm board revision, PCB schematic, PSRAM/flash configuration, pin map, battery charge path, USB and antenna connector from the exact purchased board BEFORE assigning GPIO.
Pin ownership worksheet fields: peripheral, pin(s), voltage, SPI/I2C/UART bus, CS, IRQ, DMA, boot strap conflict, sleep retention, owner, test. Devices: display, GT911 touch, keyboard/trackball, SX1262, GPS, SD, audio, optional external sensors.
Power tree: battery/USB/external → charger/protection → regulator rails → ESP32/radio/display/GPS/SD; never promise 192h on internal battery. Capture idle/receive/transmit/display-on/AP/GPS currents, thermal behavior, brownout thresholds.
Constraints: PSRAM cache allocation after internal heap reserve; never allocate radio ISR buffers in PSRAM without verified DMA support; measure stack high-water and fragmentation.
Failure cases: low battery, charge disconnect, SD hot-unplug, SPI contention, reboot during write, GPS no fix, touch unresponsive.
Acceptance: measured pin/bus matrix; no boot strap conflict; brownout recovery and 24h soak logs before expanding to longer runs. Unknown until measured.

## Z02 RF mesh / SX1262 / P1 V8 (S2)
Actual hook: MyMesh::v29SendEmergencyRaw uses createRawData() and sendFlood(); onRawDataRecv delegates to V29EmergencyFabric::onRawFrame first.
Existing contract assumes MAX_PACKET_PAYLOAD 184, header 82, AEAD tag 16, signature 64, body 22 bytes; no general chat fragmentation implied.
Packet processing: IRQ→driver frame check→length bound→protocol magic/version→signature/AEAD verification→dedup→queue/recipient→event→scheduler. No dynamic allocation in ISR.
RF profile must match P1 Pro V8 in actual interoperability tests; current profile test asserts 869.618MHz, BW62.5kHz, SF8, CR5, TX22dBm, locked. This is an existing contract assertion, NOT a determination that EIRP/duty cycle is lawful for every antenna/location. Verify applicable EU868 sub-band, antenna gain and duty cycle.
Metrics: RX RSSI/SNR, retry, queue delay, observed PDR; attach timestamp/sample count; no false wideband scan/SWR claim. Link quality display shows UNKNOWN when insufficient samples.
Tests: RF-01 V28 chat regression; RF-02 P1 V8 handshake; RF-03 duplicate frames; RF-04 queue saturation; RF-05 RF disabled fallback truthful; RF-06 duty-cycle guard under repeated P0 requests.

## Z03 Ordinary chat and emergency data model (S2/S3)
Keep existing V28 chat canonical. Proposed MessageIdentity {id128, origin, destination, channel, created_monotonic, wall_clock_optional, kind, capability, state}. No extra chat database unless migration proof.
States CREATED→QUEUED→ATTEMPTED→PEER_ACK/EXPIRED/FAILED; relay publish is not peer delivery. Dedup scope: origin+logical ID+kind, not ciphertext alone. ACK must be authenticated and recipient-bound.
Emergency Kind actual enum CheckIn, HelpRequest, MeetingPoint, Household, System; Priority actual Critical/High/Normal/Bulk; PowerMode Normal/Emergency/Critical. 192h TTL is a target in source, verify age behavior over millis wrap and power cycles.
Queues: max 512 records, seen 256, events 8 (source constants); reserve before allocation, bounded eviction, P0 starvation prevention, no loss of durable data during cache shedding.
Tests: MSG-01 RF/relay same ID; MSG-02 spoofed ACK; MSG-03 reboot during snapshot; MSG-04 TTL boundary; MSG-05 overflow and fairness; MSG-06 22-byte payload exact and 23-byte rejection.

## Z04 Cryptographic protocol and privacy (S3)
Current V29EmergencyFabric.cpp includes mbedTLS GCM/HMAC and Identity; normalizedAuthData() and mutable relay age demand a threat-model review. Verify nonce uniqueness under reboot, origin signature scope, destination binding, key rotation and recipient hint privacy. Do not treat signatures over normalized fields as protection for excluded mutable fields.
Wire diagram (from source constants): 184-byte maximum; 82-byte header, 16-byte GCM tag, 64-byte signature; 22-byte maximum structured body. Offset age=12, message ID=14, origin=30, recipient hint=62, nonce=70. Confirm exact field-by-field offsets from source before freezing wire schema.
Security threats: forged origin, replay, downgrade, stale timestamp, relay age reset, queue poisoning, flood exhaustion, rogue portal client, untrusted WiFi, firmware supply chain.
Required defenses: fail closed on auth, monotonic dedup where feasible, bounded queues, no private keys in logs/HTTP, secure credential generation, constant-time comparisons for secret tokens, transport TLS validation, explicit key revocation/identity reset.
Security acceptance: adversarial packet corpus, malformed lengths, signature/tag tampering, reboot nonce test, replay after recovery, portal token expiry and CSRF review. Independent security review still PENDING.

## Z05 Internet/WiFi/BLE/USB and phone portal (S2/S3)
WiFi: ESP32-S3 2.4GHz only. STA state OFF→CONNECTING→LOCAL_IP→INTERNET_TESTED→RELAY_AUTH; AP_PORTAL_OWNED excludes conflicting STA reconnection. Android S21 2.4GHz and iPhone hotspot tests must be hardware evidence.
Portal actual V29EmergencyPortal: 15min session, 5min idle, 3s action rate, generated SSID/password/token, DNS captive portal, local HTTP server, POST actions. Existing start() can select AP_STA when STA was active; test for STA/AP coexistence rather than claiming strict radio exclusivity.
Security zoom: token length/entropy, token leakage in query/Referer, origin checking, request-line/body limits, read timeouts, HTTP response headers, unauthenticated status leakage, captive portal probes. AP password alone is not user authentication. Treat local HTTP as unencrypted; never transmit keys.
BLE/USB: only claim services existing in inspected V28 code; require discovery, pairing and timeout contracts before expanding.
Tests: NET-01 offline RF; NET-02 AP→STA restore; NET-03 10 reconnect cycles; NET-04 captive GET no side effects; NET-05 POST replay; NET-06 malformed request and slow client without UI stall.

## Z06 320×240 Tropical UI (S4)
Information architecture: HOME (Chat, Noodmodus, Contacten, Kaart, Status, Instellingen) → each detail screen → one-step Back/Home. Every item must be validated against actual navigation capacity and existing V28 UI.
Pixel design spec PROPOSAL: 320×240 landscape, safe top status region 0–25, bottom action region 212–239, content region 26–211; revise after measured LVGL layout. Touch target >=36×36 px preferred, keyboard focus visible, trackball and touch same actions. Do not obscure emergency text with wallpaper.
Screen sheets required: U01 Home, U02 DM list, U03 DM thread, U04 channels, U05 contacts/invite, U06 emergency home, U07 emergency confirm/status, U08 network, U09 local portal, U10 offline map/GPS, U11 RF diagnostics, U12 settings, U13 safe mode/recovery, U14 help. For EACH sheet: screenshot/wireframe, widgets/IDs, default/empty/loading/offline/error/critical, navigation graph, input mapping, text copy, accessibility and RAM cost.
Tropical visual rules: compressed image manifest, high contrast overlays, no giant framebuffer allocations, fallback flat theme when asset missing/low memory. UI must never imply emergency service dispatch.
Tests: touch/trackball/keyboard traversal, readable outdoors, no clipped Dutch text, disabled action reason, 10min rapid navigation without leak. Design sheets PENDING.

## Z07 Storage and recovery (S1/S3)
Actual V29 uses SPIFFS double snapshots /v29q0.bin and /v29q1.bin, generation/checksum, PackedRecord; loadSnapshots chooses newer valid generation. Audit power loss after header/data write and verify fallback when newer load fails. Source validation alone does not prove physical power-loss safety.
Define schema version, migration rules, wear budget, minimum free space, rollback and secure erase scope. SD for optional content; absent card must not prevent emergency queue. Protect secrets in NVS/key storage; avoid storing credentials in plain logs.
Tests: corrupt newer snapshot, truncate file at every boundary, full SPIFFS, generation wrap, repeated reboot, SD removal while browsing, schema mismatch.

## Z08 GPS, offline atlas, content trust (S4/S3)
Location opt-in OFF by default; show fix age and accuracy; no GPS fix→unknown position (never fabricated). Map tiles/index on SD when available; emergency actions without map. Cached data age and geographic scope visible.
Trust labels Official/Verified Local/Trusted Contact/Community/Unverified are product labels only; define cryptographic/source verification criteria or use Unverified by default. No unverified claim styled as official alert.
Tests: no fix, stale fix, SD missing, corrupted map tile, privacy disable, no internet.

## Z09 Diagnostics, safe mode, extensions (S1/S5)
Advanced diagnostics only: heap free/largest/minimum, PSRAM, storage, queue, radio RSSI/SNR with sample source, power mode, WiFi state. User-facing alerts use clear NL language.
Safe mode: trigger after repeated boot failures; disable optional assets/portal/expansions before core radio. Require manual recovery path without Internet. Do not claim bootloader rollback until partition evidence.
NFC/IR/sensors optional and default OFF; hardware matrix must prove GPIO, 3.3V compatibility, bus sharing, current and RF impact. No 5GHz, SDR or SWR promises.
Tests: low memory, corrupt settings, stuck peripheral, optional module absent.

## Z10 Build/CI/installer/release (S5)
Single target initially: LilyGo_TDeck_companion_radio_touch. Existing workflow applies overlays V11→V29 on upstream SHA 6fe6b9f06332708b6ed0cc78958017d0dcc9e35d and runs marker-heavy contracts and PlatformIO build. Must pin PlatformIO to verified known-good version and record dependencies; no guessed pin.
Existing V29 concurrency queue:max/cancel-in-progress:false prevents V29 pending run loss, but other workflows remain separate. Inventory 44 workflows and explicitly classify active builds before changing shared concurrency.
Build stages: source audit→fast syntax/static tests→host protocol model tests→single PlatformIO compile→size/partition guard→SHA256 manifest→artifact→hardware verification→installer smoke. One runner, avoid redundant matrix jobs.
Release: one V29 PRO, signed/checksummed where supported, recovery instructions, verified compatibility table, changelog, evidence ledger. No silent production publish from test workflow.

## Z11 Quality scoring and release blockers
Per subsystem score dimensions: architecture 20, correctness 25, security 20, resource 15, UX 10, evidence 10 (100 points). Missing real tests caps score at 65; unmitigated CRITICAL caps at 40; any missing artifact is NOT VERIFIED. 9/10 requires >=90 plus all P0/P1 tests; 10/10 requires reproducible independent hardware evidence and zero release blockers.
Known blockers: V28 privacy fallback delta unported, toolchain not pinned, no measured flash/heap/power baseline, missing detailed screen wireframes, no independent security review, no hardware regression. **No coding authorization implied by this atlas.**
