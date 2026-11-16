import assert from 'node:assert/strict';
import { generateKeyPairSync } from 'node:crypto';
import { V30RelayCore } from './V30RelayCore.mjs';
import { V30GrantAuthority } from './V30GrantAuthority.mjs';
import { V30RelayAdapter } from './V30RelayAdapter.mjs';

const { privateKey, publicKey } = generateKeyPairSync('ed25519');
const authority = new V30GrantAuthority({ privateKey, issuerPublicKey: publicKey });
const relay = new V30RelayAdapter(new V30RelayCore({ttlMs: 1000}), authority);
const origin = 'aa'.repeat(32);
const consumer = 'bb'.repeat(32);
const scope = '11'.repeat(16);
const message = '01'.repeat(16);
const body = Buffer.from('opaque').toString('base64');

const push = (n, ttl = 5000) => authority.issueGrant({
  scopeId: scope, originId: origin, right: 'push',
  nowMs: n, ttlMs: ttl, nonce: n.toString(16).padStart(32, '0'),
});
const poll = (n, ttl = 5000) => authority.issueGrant({
  scopeId: scope, originId: consumer, right: 'poll',
  nowMs: n, ttlMs: ttl, nonce: (n + 100).toString(16).padStart(32, '0'),
});

assert.throws(() => relay.accept({scopeId: scope, originId: origin,
  messageId: message, ciphertext: body}, null, 0), /grant_missing/);
assert.equal(relay.accept({scopeId: scope, originId: origin,
  messageId: message, ciphertext: body}, push(0), 0).accepted, true);

assert.throws(() => relay.accept({scopeId: scope, originId: origin,
  messageId: '02'.repeat(16), ciphertext: body},
  {...push(1), scopeId: '22'.repeat(16)}, 1), /grant_signature_invalid|grant_scope_mismatch/);
assert.throws(() => relay.accept({scopeId: scope, originId: origin,
  messageId: '03'.repeat(16), ciphertext: body},
  {...push(2), signature: 'A'.repeat(86)}, 2), /grant_signature_invalid/);
assert.throws(() => relay.accept({scopeId: scope, originId: origin,
  messageId: '04'.repeat(16), ciphertext: body}, push(0), 3), /grant_replay/);
assert.throws(() => relay.accept({scopeId: scope, originId: origin,
  messageId: '05'.repeat(16), ciphertext: body}, push(10, 1000), 1010),
  /grant_expired_or_invalid/);

const pg = poll(20);
const page = relay.poll({scopeId: scope, consumerOriginId: consumer,
  nowMs: 20, authorizationGrant: pg});
assert.equal(page.items.length, 1);
assert.throws(() => relay.poll({scopeId: '22'.repeat(16),
  consumerOriginId: consumer, nowMs: 21, authorizationGrant: pg}),
  /grant_scope_mismatch/);

assert.throws(() => authority.verifyGrant({
  scopeId: scope, originId: origin, rights: ['push'], expiresAt: 1000,
  nonce: 'ff'.repeat(16),
}, {scopeId: scope, originId: origin, right: 'push', nowMs: 0}), /signature_invalid/);

console.log('V30 signed grant adapter contract PASS');
