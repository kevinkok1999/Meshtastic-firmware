# MeshOffGridNL V16 — Wi-Fi Connection Engine

V16 is a focused T-Deck / T-Deck Plus Wi-Fi reliability release based on V15.

## Three phases

1. **Connect ownership**
   - A user-requested connect takes priority over automatic rescans.
   - Arduino auto-reconnect stays disabled; the firmware has one retry owner.

2. **Deterministic association**
   - Stage 1: one attempt using the freshly selected BSSID/channel.
   - Stage 2: release that hint and retry by SSID over all 2.4 GHz channels.
   - Stage 3: soft station recovery and one final all-channel attempt.
   - The foreground attempt is bounded; it no longer sits on “Connecting…” forever.

3. **Diagnostics and validation**
   - Disconnect reasons survive stage changes.
   - UI reports the active connection stage.
   - V11/V12/V13/V15 contracts stay intact.
   - The real LilyGo_TDeck_companion_radio_touch target must compile before release.

Physical Android-hotspot testing on the affected T-Deck Plus remains the final hardware validation.
