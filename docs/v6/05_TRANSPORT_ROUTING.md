# V6 Transport, Routing & Delivery

## Separation of concerns

Transport = how bytes move.
Routing = which peer/opportunity should receive them.
Delivery = whether the logical message reached the endpoint.
Policy = which transports the user allows.

Never merge these concepts.

## User modes

### OFF_GRID
Allowed:
- LoRa
- ESP-NOW / ESP LR where supported
- XBee
- local store-carry-forward

Forbidden:
- Internet relay
- cloud lookup
- remote telemetry

### INTERNET
Allowed:
- Wi-Fi/TLS blind relay

Off-grid transports do not silently carry chat traffic.

### SMART
Only active after explicit user selection.
May use multiple permitted routes according to deterministic policy.
The user can disable individual transport classes.

## Transport API
Conceptual interface:

TransportCapabilities capabilities()
TransportHealth health()
bool start()
void stop()
SendResult send(EnvelopeView)
ReceiveBatch pollReceive()
TransportMetrics metrics()

Capabilities:
- MTU
- broadcast/unicast
- estimated latency class
- energy class
- cost class
- Internet dependency
- local-only flag

## Deterministic routing first
Do not start with a neural network.

Route score can use bounded inputs:
- recent delivery success;
- RSSI/SNR where meaningful;
- peer freshness;
- retry rate;
- queue pressure;
- energy cost;
- destination-specific history;
- relay custody availability.

Every score factor must be explainable in diagnostics.

## Contact graph
Optional V6 feature:
store coarse encounter statistics, not plaintext behavior profiles.

Example:
- peer seen count;
- last seen age;
- successful custody transfers;
- destination success class.

No precise location is required.

## Store-Carry-Forward
A relay receives an opaque envelope and may accept custody.

Custody record:
- envelope id
- opaque destination token
- expiry
- priority
- retry state
- size
- encrypted object reference

Relay never needs plaintext.

## Copy control
Prevent uncontrolled epidemic flooding.

Each message policy defines:
- maximum relay copies;
- maximum hop/custody transitions;
- expiry;
- priority;
- whether opportunistic replication is allowed.

## Inventory sync
Before bulk transfer, peers compare compact recent inventories.

Goals:
- avoid resending everything;
- minimize airtime;
- remain bounded under malicious inventories.

Inventory state is split into epochs/windows.
Never allocate based directly on peer-advertised counts.

## Fragmentation
Transport-adapter calculates usable payload after headers.
Core fragmentation remains transport-independent.

A transport can reject an envelope if:
- MTU impossible;
- policy forbids fragmentation;
- energy/duty-cycle budget exceeded.

## Reliability
Delivery states:
CREATED
PROTECTED
QUEUED
TRANSPORT_ACCEPTED
CUSTODY_ACCEPTED
DESTINATION_DELIVERED
READ
EXPIRED
FAILED_POLICY

TransportAccepted must not become a user-visible Delivered state.

## Retry
Use bounded exponential/backoff classes appropriate to transport.
Retry schedule must:
- honor user mode;
- honor expiry;
- honor regulatory/airtime constraints;
- stop on end-to-end delivery;
- persist across reboot when needed.

## Dedup
All ingress transports feed one common dedup service before expensive work where safe.
After authentication/decryption, logical duplicate suppression is rechecked.

## Loop prevention
Every envelope has bounded loop-control semantics.
Store-forward routing must not bounce indefinitely between peers.

## Congestion
Transport metrics expose queue pressure.
Router can defer low-priority traffic rather than blindly increasing retries.

## Battery-aware policy
Battery affects scheduling, not user privacy mode.

Example:
Low battery may:
- reduce discovery frequency;
- defer low-priority sync;
- preserve urgent/direct messages.

It must not turn Internet on while OFF_GRID is selected.

## Diagnostics
Advanced view can show:
- selected mode
- available transports
- reason a transport was rejected
- route score components
- retries
- queue depth
- last delivery path

Normal chat UI shows only a simple route label and delivery state.
