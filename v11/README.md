# MeshOffGridNL V11 — WadaMesh Global

V11 is a minimal-delta build on top of WadaMesh stable beta_83.

Pinned upstream:
- Repository: ALLFATHER-BV/wadamesh
- Tag: beta_83
- Commit: 6fe6b9f06332708b6ed0cc78958017d0dcc9e35d
- Target: LilyGo_TDeck_companion_radio_touch
- License: GPL-3.0-or-later

V11 keeps the normal WadaMesh/MeshCore LoRa route intact and adds an encrypted Wi-Fi global direct-message transport between V11 peers. The global transport derives a per-peer AES-256-GCM key from the existing MeshCore identity ECDH shared secret, so the broker receives ciphertext rather than chat plaintext.

The T-Deck target remains on the upstream EU868 profile at 869.618 MHz, BW 62.5 kHz, SF8 and 22 dBm. V11 explicitly caps the selectable radio power at the T-Deck/SX1262 hardware ceiling of 22 dBm. It does not add a hidden over-power path.

No SD card is required for the V11 transport. Existing WadaMesh storage fallback remains intact.

RC1 global transport covers direct chats. MeshCore rooms/channels stay on the normal MeshCore route until a separate dual-device regression pass is complete.

The default broker in RC1 is broker.emqx.io:1883 so the transport can be integration-tested without provisioning infrastructure. It is a public test broker, so it is not the long-term production relay. Payloads are application-layer encrypted before publish.
