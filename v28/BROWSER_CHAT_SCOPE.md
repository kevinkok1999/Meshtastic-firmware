# V28 Browser-Only Chat UX Contract

V28 may improve ONLY the browser/companion-web experience.

Immutable chat behavior:
- MyMesh DM send/receive semantics unchanged by the browser UX layer.
- Existing channel send/receive semantics unchanged.
- Existing RF fallback/routing behavior unchanged by browser UX.
- Existing on-device chat UI unchanged.
- Existing chat history/persistence unchanged.
- Existing WebSocket chat commands remain the actual send/open paths.

V28 browser additions:
1. Browser opens on Chats.
2. Add one visible "New chat" action.
3. Add one visible "Join #" action.
4. "New chat" presents existing contacts and existing #channels in one chooser.
5. Selecting a contact/channel invokes the already-existing browser chat commands.
6. "Join #" accepts the V28 8-character invite code.
7. After a secure join completes, the channel is added to the normal channel list and all future messages use the existing channel chat path.
8. No second chat history, no second composer, no V28-specific message bubble implementation.

User goal:
- Open browser -> Chat is already the primary screen.
- Existing chat -> tap once and type.
- New chat -> New chat -> person/#channel -> type.
- Private #channel -> Join # -> type 8-character code -> joined securely; no cryptographic material exposed.

The 8-character code is only an invite locator. It is never the actual channel secret.
