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

- Integrated optional registered-channel network challenge grant issuer and reference client. Exact legacy scope HMAC preserved, independent membership verifier + device signatures, bounded 15s challenge/60s grant, default disabled routes. Local 10/10 relay tests + isolated CI 44184513544 passed actual loopback two-client exchange/restart. No native client, public deployment, secure enrollment or real internet proof yet.

- Integrated exact native channel join/open behavior after host 44184356322 and full pinned T-Deck 44184356058 passed: no overwriting existing same-key slot, exact full padded key and slot/thread match, preserve short invite.
- Integrated readonly flash map guard and miniz source filter after full pinned firmware 44184306946 passed. The internal raw map is never automatically mounted/formatted as LittleFS; no map UI or signed production image yet.
- Installer now rejects CRC-valid invalid zlib, trailing extra streams and decoded size beyond 4 MiB; 51 local installer/map/signature cases pass.
- Integrated national-source pipeline (native PBF smoke run 44184880421 passed); ordinary pushes never download the large input. Actual national extract run 44184613196 was still running and no coverage was verified.
- Combined host and embedded checks must be rerun against this integration, and no publishing or flashing has been authorized by these development results.

- Combined host 44185369383 and national PBF smoke 44185369352 passed 5f560c59. Integrated opt-in Dispatcher idempotence and dormant qualified-radio bridge with conservative cold-start, explicit generation identity and fail-closed admission after full pinned candidate run 44184780932 success. No observer is installed in the default runtime, no production enable, no HIL and no duty compliance claim. The separate pool lifecycle observer must be independently verified before integration.

- 2026-11-18: actual combined pinned T-Deck build 44185701037 and host modules 44185700985 succeeded at 718f9688, including exact generated flash partition binary. Integrated viewport background worker candidate 44290c99 (host + full firmware 44185658673 success), bounded 512 primitive frame, cooperative cancellation, stale generation discard, explicit trust gate; no UI activation. Integrated real pinned pool observation candidate d2127060 (host + opt-in full firmware 44185727514 success); no observer installed or RF activation. Combined CI reruns on this merge.
- National source snapshot 261116 was acquired, publisher MD5 confirmed, SHA256 values observed and checked; old run 44184613196 failed at Fryslân alias before packing. Current alias fix already merged. No result or coverage claim. Browser candidate run 44185714104 failed on C++11 aggregate incompatibility; isolated repair underway. No production release.

- Added deterministic unsigned development package builder (exact padded map, bounded input, existing output refusal, validation before publication, manifest last). 56 local installer/map/signature tests passed including five new package cases. This is not a published firmware artifact or signing authority. National one-shot candidate 44311372474 launched from corrected 718f9688 snapshot; no output yet. Source acquisition not repeated on ordinary pushes.

- Integrated optional HTTPS transport for relay, exact candidate e54693de and CI 44311736834 success (13/13 tests): TLS >=1.2, strict config, real verified CA/hostname and two-client same-key encrypted exchange, TLS1.1/plaintext/incorrect identity rejected. No public endpoint or provisioning. Old localhost HTTP test-only API remains.
- Installer now requires literal production=false. RED six ambiguous/missing JSON production values accepted; GREEN 57 local tests. No production authorization.
- National run 44311372474 failed safely before PBF download: rolling PDOK response bytes differed from prior hash. Isolated small exact-byte capture validation underway (no repeated unverified PBF download). Main combined host viewport and pool merge CI 44311207301 and national smoke 44311207444 succeeded; latest package host 44311642132 succeeded. Embedded combined build pending.
