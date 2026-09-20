# V10 RF Resource Scheduler

## 1. Goal

Prevent the five logical bearers from interfering with each other or corrupting radio state.

## 2. SUBGHZ transitions

Home:
SX1262 LoRa + MeshCore receive.

To GFSK:
1. block new MeshCore TX requests;
2. wait for in-flight LoRa operation to finish with timeout;
3. stop LoRa receive;
4. snapshot required LoRa profile/state;
5. initialize known V10 GFSK profile;
6. probe/send/wait ACK;
7. return radio to standby;
8. restore exact LoRa profile;
9. restore IRQ/receive state;
10. release SUBGHZ lock.

Any error jumps to RESTORE_LORA.

If restoration fails:
- hard-reset/reinitialize SX1262 using known-safe LoRa init path;
- disable GFSK for current boot;
- continue on LoRa/2.4 GHz bearers.

## 3. RF24 transitions

ESP-NOW LR is the preferred home mode for current V7/V8/V9 compatibility.

### ESP-NOW -> Wi-Fi LR
- pause new ESP-NOW DirectLink sends;
- preserve peer identity/session;
- use deterministic pair role;
- reconfigure Wi-Fi role/protocol;
- connect;
- exchange UDP probe/data;
- disconnect/restore station mode;
- restore ESP-NOW and peer table.

### ESP-NOW/Wi-Fi -> BLE Coded
BLE Coded should be entered only when:
- the peer advertises capability;
- range policy requests it;
- no Wi-Fi frame is awaiting ACK;
- RF24 scheduler grants a BLE window.

After BLE transaction:
- return to home RF24 mode;
- verify ESP-NOW health with a bounded probe if needed.

## 4. Deterministic Wi-Fi LR role election

No manual AP selection.

Both peers compute:
role = compare(localPairIdentityHash, remotePairIdentityHash)

Lower lexical identity hash:
- DIRECT_AP

Higher:
- DIRECT_STA

SSID and password material are derived from the authenticated PairSession, not shown as a public hotspot credential.

If role bring-up repeatedly fails:
- one controlled role-swap experiment is allowed;
- then disable Wi-Fi LR for current session and fall back.

## 5. Timeslicing

Scheduler has:
- urgent message window;
- measurement window;
- cooldown.

It may skip a scheduled probe when:
- a real message is waiting;
- battery policy blocks it;
- another bearer is in recovery.

No sub-second endless protocol rotation.

## 6. Transition cost

Every bearer score includes mode transition cost.

A slightly better bearer is not selected if switching into it would cost more time/airtime than completing on the current healthy bearer.

This avoids route thrashing.
