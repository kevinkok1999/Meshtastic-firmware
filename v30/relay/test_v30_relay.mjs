import assert from 'node:assert/strict';
import { V30RelayCore } from '../relay/V30RelayCore.mjs';

const originA = 'aa'.repeat(32);
const originB = 'bb'.repeat(32);
const scopeOne = '11'.repeat(16);
const scopeTwo = '22'.repeat(16);
const messageOne = '01'.repeat(16);
const messageTwo = '02'.repeat(16);
const body = Buffer.from('opaque ciphertext bytes').toString('base64');

const relay = new V30RelayCore({ maxEntries: 2, ttlMs: 1000 });
const envelope = { originId: originA, messageId: messageOne,
  scopeId: scopeOne, ciphertext: body };

assert.deepEqual(relay.accept(envelope, 0), {
  accepted: true, duplicate: false, expiresAt: 1000,
});
assert.deepEqual(relay.accept(envelope, 1), {
  accepted: true, duplicate: true, expiresAt: 1000,
});
assert.deepEqual(relay.accept({...envelope, ciphertext: Buffer.from('changed').toString('base64')}, 2), {
  accepted: false, duplicate: false, error: 'idempotency_conflict',
});
assert.equal(relay.accept({...envelope, originId: originB}, 2).accepted, true);
assert.equal(relay.accept({...envelope, scopeId: scopeTwo}, 2).accepted, false);

assert.equal(relay.poll(scopeOne, originB, 2).length, 1);
assert.equal(relay.poll(scopeOne, 'cc'.repeat(32), 2).length, 2);
assert.equal(relay.poll(scopeTwo, originB, 2).length, 0);

assert.throws(() => relay.accept({...envelope, ciphertext: 'not-base64'}, 3),
  /ciphertext_invalid/);
assert.throws(() => relay.accept({...envelope, plaintext: 'secret'}, 3),
  /field_not_allowed/);
assert.throws(() => relay.accept({...envelope, originId: 'aa'}, 3),
  /origin_invalid/);

const expired = relay.accept({
  originId: originB, messageId: messageTwo, scopeId: scopeOne, ciphertext: body,
}, 1000);
assert.equal(expired.accepted, true);
assert.equal(relay.size(2000), 0);

const bounded = new V30RelayCore({ maxEntries: 1, ttlMs: 1000 });
assert.equal(bounded.accept(envelope, 0).accepted, true);
assert.deepEqual(bounded.accept({...envelope, messageId: messageTwo}, 1), {
  accepted: false, duplicate: false, error: 'capacity',
});

console.log('V30 relay core contract PASS');
