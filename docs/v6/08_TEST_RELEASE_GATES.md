# V6 Test Strategy & Release Gates

## Rule
A firmware that compiles is not a releasable firmware.

## Test pyramid

### Pure unit tests
- envelope codec
- dedup
- expiry
- route scoring
- queue eviction
- retry math
- state machines
- storage transaction logic
- crypto adapter test vectors

### Property tests
Examples:
- encode/decode roundtrip
- fragmentation/reassembly roundtrip
- duplicate delivery never creates duplicate chat entry
- expiry never resurrects
- route score stays bounded

### Fuzzing
Targets:
- wire parser
- fragment parser
- inventory parser
- contact/session control
- group control
- storage record decoder

Corpus includes malformed and truncated inputs.

### Network simulation
Simulate:
- packet loss 0–80%
- duplication
- reorder
- asymmetric links
- moving nodes
- node reboot
- relay disappears
- Internet outage
- delayed contact
- queue saturation
- corrupted packet
- replay
- clock absent/wrong

Run deterministic seeds in CI.

### Power-loss simulation
Inject reset/failure at every critical storage commit boundary.

### Hardware-in-loop
Minimum physical matrix:
- 2 T-Deck Plus direct
- 3+ nodes multi-hop
- T-Deck + P1/backbone where applicable
- no XBee installed
- XBee installed
- Internet on/off
- Wi-Fi credential failure
- USB/browser flash/recovery

## Security tests
- replay
- wrong identity
- key changed
- corrupted AEAD
- downgrade attempt
- old protocol version
- malformed extension
- queue exhaustion
- oversized fragments
- storage rollback
- unsigned update in secure profile

## Privacy tests
Automated assertions:
- no plaintext message in production logs
- no Wi-Fi password in logs
- no human display name in native radio envelope unless explicitly protected
- OFF_GRID causes zero Internet message transport
- location defaults OFF
- analytics absent/disabled

## Performance budgets
Set before implementation:
- boot time budget
- idle RAM budget
- crypto operation RAM peak
- max message queue RAM
- max flash usage
- direct message latency targets
- relay sync time target
- battery/discovery budget

Build fails when hard budgets regress beyond agreed threshold.

## Release pipeline
1. static analysis/lint
2. unit/property tests
3. fuzz regression corpus
4. simulator scenarios
5. firmware compile
6. image layout validation
7. signed manifest generation
8. hardware candidate test
9. release approval
10. production publication

No release branch published before all automated pre-hardware gates pass.

## Candidate labels
DEV
ALPHA
BETA
RC
STABLE

"Stable" requires real hardware validation.

## Reproducibility
Release manifest records:
- source commit
- toolchain versions
- environment name
- partition map
- binary size
- hashes
- protocol version
- crypto suite registry version
- test run IDs

## Rollback
Developer profile:
- easy full flash recovery.

Secure production:
- signed A/B OTA/rollback strategy designed before locking device;
- rollback security policy prevents booting known-vulnerable forbidden builds where required.

## Failure policy
A failed test does not get bypassed by a second parallel release.
One canonical candidate commit owns all downstream artifacts.
