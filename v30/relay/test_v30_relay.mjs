import assert from 'node:assert/strict';
import { V30RelayCore } from '../relay/V30RelayCore.mjs';

const originA = 'aa'.repeat(32);
const originB = 'bb'.repeat(32);
const consumer = 'cc'.repeat(32);
const scopeOne = '11'.repeat(16);
const scopeTwo = '22'.repeat(16);
const messageOne = '01'.repeat(16);
const messageTwo = '02'.repeat(16);
const body = Buffer.from('opaque ciphertext bytes').toString('base64');
const grant = (scopeId, originId, rights, expiresAt = 10000) =>
  ({scopeId, originId, rights, expiresAt});

const relay = new V30RelayCore({ maxEntries: 4, ttlMs: 1000 });
const envelope = {originId: originA, messageId: messageOne,
  scopeId: scopeOne, ciphertext: body};
const pushA = grant(scopeOne, originA, ['push']);

assert.throws(() => relay.accept(envelope, 0), /authorization_grant_missing/);
assert.throws(() => relay.accept(envelope, 0, grant(scopeTwo, originA, ['push'])),
  /authorization_grant_mismatch/);
assert.throws(() => relay.accept(envelope, 10001, pushA),
  /authorization_grant_mismatch/);
assert.deepEqual(relay.accept(envelope, 0, pushA).accepted, true);
assert.equal(relay.accept(envelope, 1, pushA).duplicate, true);
assert.equal(relay.accept({...envelope, ciphertext: Buffer.from('changed').toString('base64')},
  2, pushA).error, 'idempotency_conflict');
assert.equal(relay.accept({...envelope, originId: originB}, 2,
  grant(scopeOne, originB, ['push'])).accepted, true);

const pollGrant = grant(scopeOne, consumer, ['poll']);
assert.throws(() => relay.poll({scopeId: scopeOne, consumerOriginId: consumer,
  nowMs: 2, authorizationGrant: grant(scopeOne, originA, ['poll'])}),
  /authorization_grant_mismatch/);
let page = relay.poll({scopeId: scopeOne, consumerOriginId: consumer, nowMs: 2,
  limit: 1, authorizationGrant: pollGrant});
assert.equal(page.items.length, 1);
assert.equal(page.nextCursor, page.items[0].sequence);
page = relay.poll({scopeId: scopeOne, consumerOriginId: consumer, nowMs: 2,
  limit: 1, cursor: page.nextCursor, authorizationGrant: pollGrant});
assert.equal(page.items.length, 1);
assert.equal(page.items[0].originId, originB);
assert.equal(relay.poll({scopeId: scopeTwo, consumerOriginId: consumer, nowMs: 2,
  authorizationGrant: grant(scopeTwo, consumer, ['poll'])}).items.length, 0);

assert.throws(() => relay.accept({...envelope, ciphertext: 'not-base64'}, 3, pushA),
  /ciphertext_invalid/);
assert.throws(() => relay.accept({...envelope, plaintext: 'secret'}, 3, pushA),
  /field_not_allowed/);
assert.throws(() => relay.accept({...envelope, originId: 'aa'}, 3, pushA),
  /origin_invalid/);

const expired = relay.accept({originId: originB, messageId: messageTwo,
  scopeId: scopeOne, ciphertext: body}, 1000, grant(scopeOne, originB, ['push']));
assert.equal(expired.accepted, true);
assert.equal(relay.size(2000), 0);
assert.equal(relay.accept({originId: originB, messageId: messageTwo,
  scopeId: scopeOne, ciphertext: body}, 2001,
  grant(scopeOne, originB, ['push'])).accepted, true);

const bounded = new V30RelayCore({ maxEntries: 1, ttlMs: 1000 });
assert.throws(() => new V30RelayCore({maxEntries: 4097}), /max_entries_invalid/);
assert.equal(bounded.accept(envelope, 0, pushA).accepted, true);
assert.deepEqual(bounded.accept({...envelope, messageId: messageTwo}, 1,
  pushA), {accepted: false, duplicate: false, error: 'capacity'});

console.log('V30 relay core contract PASS');
