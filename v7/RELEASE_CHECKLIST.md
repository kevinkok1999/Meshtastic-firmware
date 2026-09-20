# V7 Release Checklist

## Source provenance
- [ ] Saitama base commit equals 16598b4e6a7eabd6195d06d2a3abc1b3b448f368.
- [ ] MeshCore submodule equals d92964352441e53b93e8667b802e04f6e072b39e.
- [ ] GPL-3.0-or-later notices retained.
- [ ] V7 changes documented.

## Security/privacy
- [ ] No private chat payload is downgraded to plaintext for ESP-NOW.
- [ ] Contact/MAC binding is documented and tested.
- [ ] Discovery exposes no unnecessary private content.
- [ ] Malformed/oversized frames fail closed.
- [ ] Reassembly buffers have hard bounds/timeouts.
- [ ] Retries are bounded.

## Compatibility
- [ ] LORA_ONLY parity passed.
- [ ] V7 ↔ stock Saitama/MeshCore via LoRa passed.
- [ ] MeshCore repeater path passed.
- [ ] BLE on/off/connected coexistence passed.

## Artifact
- [ ] t-deck target only.
- [ ] 16MB / DIO verified.
- [ ] app-only binary available.
- [ ] merged binary available.
- [ ] merged binary boots after full flash.
- [ ] exact byte size recorded.
- [ ] SHA-256 recorded.
- [ ] release notes include upstream base and MeshCore lock.
- [ ] artifact is immutable after installer metadata is published.

## Website preview
Production is not touched until this section passes.
- [ ] Create website feature branch.
- [ ] Add v7 to RELEASE_KEYS.
- [ ] Add V7 config using exact asset filename, size and SHA-256.
- [ ] Add server-side asset proxy/pin with size validation.
- [ ] Add V7 selection card to NL/EN/DE.
- [ ] Do not change V1-V6 objects/artifacts.
- [ ] Generate/inspect Vercel Preview.
- [ ] Verify page, API asset route and installer JS on preview.
- [ ] Flash a real T-Deck Plus from preview.
- [ ] Read-back verification passes.
- [ ] Reboot into V7 succeeds.
- [ ] Smoke-test messaging after browser flash.

## Production
- [ ] Preview evidence attached to release issue.
- [ ] Production change limited to already-tested website commit.
- [ ] Production deployment healthy.
- [ ] Production installer V1-V6 still work/select correctly.
- [ ] Production V7 metadata exactly equals released artifact.
- [ ] Rollback target is known before promotion.
