# V6 UX / Product Specification

## UX principle
The system may be complex. The interface must not be.

90% of users should mainly use:
- Chats
- Contacts
- Connection

## Navigation

### Home / Chats
Header:
- privacy lock status
- active connection mode
- battery

Conversation list:
- contact/group name
- latest message preview (local plaintext only)
- time
- unread count
- simple delivery warning if needed

### Chat
Show:
- messages
- send box
- verified-contact shield
- delivery state

Tap message -> details:
- encrypted: yes
- delivered/read state
- route class: Off-grid / Internet / Smart
- optional advanced route details

Never expose MQTT/ESP-NOW terminology in normal view.

## Connection screen

OFF-GRID
"Geen internet nodig. Alleen lokale/radioverbindingen."

INTERNET
"Wereldwijd via je Wi-Fi/internetverbinding."

SMART
"Gebruikt alleen de verbindingstypen die jij hier toestaat."

Changing mode is explicit and immediately visible.

## First-run onboarding

Screen 1: Welcome
"Privé communiceren met én zonder internet."

Screen 2: Display name
Explain it is local/contact-facing, not an account registration.

Screen 3: Device unlock/PIN
Explain purpose without overclaiming.

Screen 4: Privacy defaults
- Location OFF
- Analytics OFF
- Internet relay OFF until Internet/Smart selected

Screen 5: Connection mode
Off-grid or Internet.

Screen 6: Add first contact
QR scan / show QR / nearby discovery.

## Contact adding

Primary:
- Show my QR
- Scan QR

Secondary:
- Nearby discovery
- Contact code

After verification:
"Identiteit geverifieerd"

Key change:
"De beveiligingsidentiteit van dit contact is veranderd."
Actions:
- Verify again
- Keep unverified
- Block

Never silently preserve verified state after a key change.

## Privacy screen
Simple status:
- End-to-end encryption
- Contact verification
- Location sharing
- Analytics
- Encrypted local storage
- Active connection mode

Advanced privacy:
- metadata explanation
- rotating discovery state
- message retention
- relay retention
- diagnostic export

## Wi-Fi setup
Normal network picker.
No MQTT fields in normal UI.

Advanced:
- custom relay/server settings only for expert mode.

## Delivery states
User vocabulary:
Preparing
Waiting for connection
Sent
On the way via mesh
Delivered
Read
Expired / Could not deliver

No low-level error code as primary message.

## Offline behavior
If no permitted path:
"Bericht veilig opgeslagen. V6 probeert opnieuw zodra een toegestane verbinding beschikbaar is."

Do not auto-switch modes unless SMART is explicitly active.

## Group creation
Name -> select members -> create.
Crypto epoch/member changes happen invisibly.
Show a security event in chat when membership changes.

## Battery
Profiles:
- Balanced
- Long battery life
- Maximum connectivity

A profile may tune discovery/retry frequency.
It may not violate selected privacy/connection mode.

## Updates
Update card:
- version
- signed/verified badge
- short changelog
- Install

Flow:
Download -> Verify -> Install -> Restart -> Health check

If update fails, preserve old bootable app where platform supports rollback.

## Recovery UX
Recovery mode:
- Restart
- Repair/verify storage
- Restore signed firmware
- Restore encrypted identity backup
- Factory reset
- Export diagnostics

## Advanced screen
Hidden behind explicit "Advanced":
- radio preset
- transport enable/disable inside allowed mode
- route metrics
- packet stats
- developer logs
- protocol version
- firmware build/hash

## Accessibility
- never encode state by color alone;
- large hit targets;
- readable contrast;
- status icons plus text;
- Dutch/English/German strings kept separate from logic;
- keyboard-only navigation for all critical actions.

## Error language
Bad:
"ERR_TRANSPORT_TIMEOUT 0x07"

Good:
"De ontvanger is nog niet bereikbaar. Je bericht blijft opgeslagen."

Technical code remains available in details for support.
