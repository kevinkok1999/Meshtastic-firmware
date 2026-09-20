# V9 Reach Engine Contract

## 1. Inputs

For each candidate bearer/path:
- normalized link confidence;
- delivery EWMA;
- fragment loss EWMA;
- ACK latency EWMA;
- retry EWMA;
- airtime estimate;
- queue pressure;
- energy class;
- metric age;
- hop count;
- path diversity tag.

## 2. Outputs

ReachDecision:
- primary path;
- optional backup path;
- fragment payload size;
- ACK window size;
- retry rounds;
- FEC profile;
- redundant-send flag;
- store-forward allowed;
- decision reason code.

## 3. Deterministic policy

The production policy is deterministic and inspectable.

No opaque ML model decides whether a message is delivered.

An optional learning layer may update bounded metric priors, but:
- it cannot create new RF settings;
- it cannot exceed validated profile bounds;
- it cannot disable security;
- all final decisions pass deterministic guardrails.

## 4. Hysteresis

Prevent route flapping.

Switch primary route only when:
- current route becomes invalid; or
- challenger exceeds current score by threshold for a minimum observation period.

Emergency failure may switch immediately.

## 5. Adaptive packet sizing

Example profile classes, not hard-coded byte values:
- STRONG: large fragment profile;
- GOOD: medium-large;
- MARGINAL: medium-small;
- POOR: small.

Actual byte sizes come from negotiated MTU and benchmark data.

## 6. Recovery ladder

For a failed delivery round:

1. selective resend on same path if confidence remains adequate;
2. enable/increase bounded FEC if loss pattern supports it;
3. move to backup path;
4. use redundant primary+backup for IMPORTANT traffic;
5. store encrypted envelope if no viable path exists.

The ladder is bounded and must never create retry storms.

## 7. Direct versus mesh

A direct path does not automatically outrank a mesh path.

V9 may choose:
T-Deck -> relay -> relay -> destination
over
T-Deck -> weak direct destination

when measured delivery probability is higher.

## 8. Route cache

Per destination keep a bounded cache:
- current primary;
- backup;
- recent successful next hops;
- last successful bearer;
- last failure reason;
- expiry.

No unbounded topology database on the T-Deck.

## 9. Congestion awareness

Use local indicators:
- RF busy/CAD results where available;
- queue depth;
- ACK delay;
- retry rise.

When congestion rises:
- back off;
- reduce redundant traffic;
- consider alternate bearer/path;
- avoid synchronized relay retransmissions.

## 10. Metrics telemetry

Diagnostics may display:
- chosen path;
- reason;
- confidence;
- hop count;
- retries;
- FEC used;
- stored/forwarded state;
- queue pressure.

Metrics must not expose private message plaintext.
