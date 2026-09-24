# V28 Secure 8-Character Channel Join Codes

Status: V28 ONLY. V27 and all earlier releases remain unchanged.

## Goal

Private #channels must be easy to join without exposing the real 128-bit MeshCore/V28 channel secret.

Normal user flow:
1. Creator opens #channel.
2. Chooses "Private with code".
3. V28 shows an 8-character join code such as K7M4-P9QX.
4. Another V28 user enters the code.
5. A signed join request reaches the channel owner/admin.
6. Owner/admin approves.
7. The real channel secret is sent end-to-end encrypted to the joining device.
8. Both devices now use the same normal V28/MeshCore channel.

## Critical security rule

The 8-character code is NEVER:
- the AES key;
- the MeshCore channel secret;
- directly expanded into the channel secret.

It is only a human-friendly invitation locator.

The actual channel secret remains random 128-bit material generated/stored on trusted devices.

## Code format

Alphabet:
ABCDEFGHJKLMNPQRSTUVWXYZ23456789

Properties:
- 32 symbols (5 bits per character);
- 8 characters = 40-bit invitation namespace;
- case-insensitive input;
- display as XXXX-XXXX;
- excludes ambiguous I, O, 0 and 1;
- server stores SHA-256(code), not plaintext code;
- code expires and can be rotated/revoked.

40 bits is intentionally NOT treated as cryptographic key strength. Online guessing is controlled by signed identity, rate limits, expiry and owner approval.

## Private-channel join protocol

CREATE:
- owner device already holds the random channel secret locally;
- owner requests an invite code;
- server stores only code hash, owner public key, opaque channel capability/id and expiry;
- server never receives the channel secret.

REQUEST:
- joining device submits code + its device public key/signature;
- server resolves code hash and creates a pending join request;
- no secret is returned.

APPROVE:
- owner/admin device receives pending requester public key;
- owner approves locally;
- owner encrypts the existing channel secret to the requester using the V28 pairwise secure control path;
- encrypted join bundle is delivered through the normal V28 relay;
- server still never learns the channel secret.

DENY/REVOKE:
- owner can deny a request;
- invite code can be rotated/revoked without changing the channel secret;
- removing a member later requires a group-key epoch/rotation if future messages must become inaccessible to that member.

## UX

Public #channel:
- join by hashtag;
- clearly labelled Public.

Private #channel:
- Join with 8-character code;
- status: Waiting for approval;
- after approval: Joined securely.

Advanced:
- QR invitation may carry a high-entropy one-time token for instant pairing later.
- QR is separate from the 8-character human code.

## Abuse controls

- signed device requests;
- per-device rate limit;
- per-code rate limit;
- maximum pending requests per invite;
- automatic expiry;
- no response reveals whether a guessed code is close to a real one;
- repeated failures enter backoff;
- blocked devices remain blocked across Internet and RF-facing UI.

## Compatibility

- only V28 understands short join codes;
- after successful join, the actual channel remains a normal channel with the real 128-bit secret;
- P1 Pro V8 / V27 / V26 / V19 are not modified.
