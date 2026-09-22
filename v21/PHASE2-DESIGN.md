# V21 Phase 2 — Full Firmware WiFiManager Design

Phase 2 begins only after Phase 1 produces a hardware-proven Golden result.

## One owner

V21 will have exactly one component responsible for connection lifecycle:

`V21WifiManager`

It owns:
- station initialization;
- event capture;
- connection state;
- backoff timing;
- scan arbitration;
- diagnostics.

It does not own:
- UI rendering;
- MeshCore/LoRa;
- Bluetooth;
- Local AI;
- message routing.

## Normal connection path

```
BOOT
  -> STA_START
  -> IDLE
  -> CONNECTING
  -> STA_CONNECTED
  -> WAIT_DHCP
  -> GOT_IP / ONLINE
```

On disconnect:

```
DISCONNECTED
  -> record numeric reason
  -> schedule bounded backoff
  -> reconnect from manager task/loop
```

No driver mutations are performed from the Wi-Fi event callback.

## Behaviors removed from the normal path

The full V21 integration must not inherit the experimental V13-V19 join behavior that caused ambiguity:

- no `WiFi.disconnect()` before every join;
- no routine `WIFI_OFF -> WIFI_STA` cycling;
- no BSSID pinning;
- no channel pinning;
- no forced PMF/SAE mode;
- no forced Wi-Fi PHY/bandwidth;
- no erase/reset during a normal retry;
- no UI scan while CONNECTING or WAIT_DHCP;
- no second reconnect owner.

## Scan policy

Scanning is user/UI initiated only while the manager is IDLE or ONLINE when safe.

If a connect is in progress, a scan request is rejected/deferred rather than competing with association.

## BLE reintroduction gate

The first full V21 integration is tested with BLE/NimBLE disabled.

Only after S21 Wi-Fi passes:
1. enable BLE;
2. repeat the identical S21 matrix;
3. compare reason codes and reconnect reliability.

If Wi-Fi regresses only when BLE is enabled, coexistence becomes a directly demonstrated failure mode rather than a guess.

## Framework selection

The hardware A/B result decides whether V21 stays on the legacy Arduino/IDF base or migrates to the modern pioarduino base.

No migration is made solely because a newer version exists.
