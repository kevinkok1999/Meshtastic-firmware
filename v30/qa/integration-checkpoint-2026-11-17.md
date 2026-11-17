# V30 integration checkpoint

No release or hardware ACK claim. V29 branch remains unchanged.

- Integrated native metadata/name lookup, CRC and corrected pack limits. Host map run 43982231201 (red 43982115956: 82812 > 65536). No geometry decode or native map display yet.
- Integrated bounded HTTP/1.1 parser and native-action approval state machine, runs 43981905451 and 43982249044. Actual portal patch still isolated pending embedded build and physical approval validation.
- Integrated server-side signed grant prototype with device proof + application authorization callback, run 43982610858. Not an HTTP endpoint, not deployed, no database or persistent replay protection; no global chat claim.
- Opt-in Dispatcher hook compiled in isolated run 43981960106; actual patched dependency post-build evidence and RF activation remain open.
- Latest main baseline ac5eb3d had complete CI success run 43981237884; current merge will get its own build/tests.

- Actual emergency portal candidate reconstructed and compiled in run 43982594782 (DEV T-Deck). Integrated into standard V30 applier with idempotent fixture checks. Physical touch provenance and hardware operation still require HIL.
- Central isolated host run 43982743373 completed successfully on 7b2ddcb4; embedded run 43982803887 remained in progress at this checkpoint.

- Actual local HTTP relay candidate integrated after run 43983477658: two independent Ed25519 clients, client-encrypted payload message exchange over loopback HTTP, durable snapshot/replay after restart, denied invalid and oversized requests and fail-closed disk failures. This is an unexposed single-process development service, not a worldwide HTTPS deployment; no device onboarding, deployed TLS, distributed persistence or native/browser sync proof yet.

- Real file journal backend integrated as dormant module, with exact extent and zero-slot verification, no corrupt-record overwrite, bounded offset arithmetic, durable write/clear and reopen. Code compiled for the actual pinned T-Deck target run 43983051935; additional clear regression passed host on 30a8e56f. Mount/provisioning, actual message-journal binding, and physical power-cut remain unverified; no activation.

- 2026-11-17: merged native streaming map reader + miniz and builder decoded-budget enforcement, isolated host + ESP32-S3 module run 43983880432; merged installer padded-image/CRC/metadata verification run 43983923659. No signed map, real geographic coverage, native renderer or live flash activation yet. Combined T-Deck reconstruction to be revalidated.
- Prior complete main T-Deck run 43983670079 and host run 43983670118 both succeeded on b84cdfd3.

- Added detached Ed25519 signature gate with explicit independent raw-public-key SHA256 pin. Local red: 6 failing assertions against unsigned validation; green: 43 installer/map/signature tests. Real production signing key and browser installer not provisioned; production manifests still refused.

- Red/green partition binary gate: 5 regressions initially failed, now all 48 local installer/map/signature tests pass. Validate the actual PlatformIO-generated partition binary in the pinned firmware CI as well. The previous combined main embedded run 44183448985 (550fc8f5) succeeded; NanoMapPack.cpp compiled but miniz source filter still requires actual linker integration, being handled separately.
