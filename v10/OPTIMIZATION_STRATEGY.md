# V10 Optimization Strategy

## DirectLink-only simplification

Remove V10 policy dependence on:
- relay scoring;
- topology discovery;
- multipath through third nodes;
- relay reputation;
- store-and-forward through peers.

Keep only:
- A/B pair state;
- five bearer metrics;
- two RF-resource schedulers;
- one primary + one backup decision.

## Probe economy

Use probes only when metric uncertainty justifies them.

If a bearer has:
- recent successful ACK;
- stable latency;
- low retry rate;

do not probe it again before every message.

## Fast send path

If the current primary has fresh GOOD/STRONG confidence:
send immediately.

Do not delay a normal chat message while benchmarking every bearer.

## Uncertain-link path

If primary confidence is stale/marginal:
- probe at most two best candidates;
- choose winner;
- send.

## Failure path

1. selective retransmission on primary;
2. backup logical bearer;
3. prefer different physical RF resource when possible;
4. final robust LoRa fallback;
5. report failure.

No infinite retry.

## Pair learning

Persist only bounded coarse history:
- last successful bearer;
- delivery EWMA per bearer;
- latency class;
- failure streak;
- preferred backup.

Do not persist a precise movement/location history for routing.

## Battery

Long-range mode is allowed to spend more energy than normal mode, but V10 must retain:
- max probe budget;
- max retry budget;
- max radio transition count per message;
- low-battery policy.

## Diagnostics

Developer view:
- bearer selected;
- physical RF resource;
- link confidence;
- probe result;
- retry/fallback;
- transition failures;
- delivery time.

Normal user view stays one chat.
