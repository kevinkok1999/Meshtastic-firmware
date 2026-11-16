import assert from 'node:assert/strict';
import { generateKeyPairSync, sign } from 'node:crypto';
import { V30RelayCore } from './V30RelayCore.mjs';
import { V30GrantAuthority, canonicalIssueChallenge } from './V30GrantAuthority.mjs';
import { V30RelayAdapter } from './V30RelayAdapter.mjs';

function rawPublic(publicKey) {
  return publicKey.export({format: 'der', type: 'spki'}).subarray(-32).toString('hex');
}
const issuer = generateKeyPairSync('ed25519');
const device = generateKeyPairSync('ed25519');
const wrongDevice = generateKeyPairSync('ed25519');
const consumerDevice = generateKeyPairSync('ed25519');
const authority = new V30GrantAuthority({
  privateKey: issuer.privateKey, issuerPublicKey: issuer.publicKey,
});
const relay = new V30RelayAdapter(new V30RelayCore({ttlMs: 1000}), authority);
const origin = rawPublic(device.publicKey);
const wrongOrigin = rawPublic(wrongDevice.publicKey);
const consumer = rawPublic(consumerDevice.publicKey);
const scope = '11'.repeat(16);
const message = '01'.repeat(16);
const body = Buffer.from('opaque').toString('base64');
const approved = ({scopeId, originId, right}) =>
  scopeId === scope && originId === origin && right === 'push';
function issuePush(n, ttl = 5000, nonce = n.toString(16).padStart(32, '0'),
                   authorize = approved, key = device.privateKey) {
  const expiresAt = n + ttl;
  const challenge = canonicalIssueChallenge({
    scopeId: scope, originId: origin, right: 'push', expiresAt, nonce,
  });
  return authority.issueGrantForDevice({
    scopeId: scope, originId: origin, right: 'push', nowMs: n, ttlMs: ttl,
    nonce, authorize, deviceSignature: sign(null, challenge, key).toString('base64url'),
  });
}
function issuePoll(n, nonce = (n + 100).toString(16).padStart(32, '0')) {
  const expiresAt = n + 5000;
  const challenge = canonicalIssueChallenge({
    scopeId: scope, originId: consumer, right: 'poll', expiresAt, nonce,
  });
  return authority.issueGrantForDevice({
    scopeId: scope, originId: consumer, right: 'poll', nowMs: n, ttlMs: 5000,
    nonce, authorize: ({right}) => right === 'poll',
    deviceSignature: sign(null, challenge, consumerDevice.privateKey).toString('base64url'),
  });
}

assert.throws(() => authority.issueGrant({
  scopeId: scope, originId: origin, right: 'push', nowMs: 0, ttlMs: 5000, nonce: '00'.repeat(16),
}), /authorization_required/);
assert.equal(relay.accept({scopeId: scope, originId: origin, messageId: message,
  ciphertext: body}, issuePush(0), 0).accepted, true);

assert.throws(() => issuePush(1, 5000, '01'.repeat(16), approved,
  wrongDevice.privateKey), /device_signature_invalid/);
assert.throws(() => issuePush(2, 5000, '02'.repeat(16), () => false),
  /authorization_denied/);
assert.throws(() => issuePush(3, 5000, '00'.repeat(16)), /issue_nonce_replay/);

const pg = issuePoll(20);
const page = relay.poll({scopeId: scope, consumerOriginId: consumer,
  nowMs: 20, authorizationGrant: pg});
assert.equal(page.items.length, 1);
assert.throws(() => relay.accept({scopeId: scope, originId: origin,
  messageId: '02'.repeat(16), ciphertext: body}, issuePush(10, 1000), 1010),
  /grant_expired_or_invalid/);
assert.throws(() => relay.poll({scopeId: '22'.repeat(16),
  consumerOriginId: consumer, nowMs: 21, authorizationGrant: pg}),
  /grant_scope_mismatch/);
assert.notEqual(origin, wrongOrigin);

console.log('V30 signed device grant adapter contract PASS');
