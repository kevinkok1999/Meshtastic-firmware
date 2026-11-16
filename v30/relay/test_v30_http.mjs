import assert from 'node:assert/strict';
import { generateKeyPairSync, sign, randomBytes, createCipheriv, createDecipheriv } from 'node:crypto';
import { once } from 'node:events';
import { request } from 'node:http';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createV30RelayService, canonicalHttpRequest } from './V30RelayHttp.mjs';
import { V30GrantAuthority, canonicalIssueChallenge } from './V30GrantAuthority.mjs';
import { V30FileStorage } from './V30FileStorage.mjs';

const issuer = generateKeyPairSync('ed25519');
const alice = generateKeyPairSync('ed25519');
const bob = generateKeyPairSync('ed25519');
const origin = k => k.publicKey.export({format: 'der', type: 'spki'}).subarray(-32).toString('hex');
const scope = '11'.repeat(16);
const nowMs = 10000;
const authority = new V30GrantAuthority({privateKey: issuer.privateKey});
function grant(device, right) {
  const originId = origin(device);
  const nonce = randomBytes(16).toString('hex');
  const expiresAt = nowMs + 60000;
  const deviceSignature = sign(null, canonicalIssueChallenge({
    scopeId: scope, originId, right, nonce, expiresAt,
  }), device.privateKey).toString('base64url');
  return authority.issueGrantForDevice({scopeId: scope, originId, right, nowMs,
    ttlMs: 60000, nonce, deviceSignature,
    authorize: q => q.scopeId === scope && [origin(alice), origin(bob)].includes(q.originId)});
}
function signed(device, path, body) {
  return {...body, deviceSignature: sign(null, canonicalHttpRequest(path, body),
    device.privateKey).toString('base64url')};
}
const directory = await mkdtemp(join(tmpdir(), 'v30-relay-'));
const storage = new V30FileStorage(join(directory, 'state.json'));
let server;
async function start() {
  server = await createV30RelayService({issuerPublicKey: issuer.publicKey, storage, now: () => nowMs});
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  return 'http://127.0.0.1:' + server.address().port;
}
async function stop() {
  server.closeAllConnections();
  await new Promise((resolve, reject) => server.close(e => e ? reject(e) : resolve()));
}
async function post(url, path, body) {
  return fetch(url + path, {method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(body)});
}
try {
  let url = await start();
  // Encryption/decryption happens only at the clients. Server sees opaque bytes.
  const key = randomBytes(32), iv = randomBytes(12);
  const cipher = createCipheriv('aes-256-gcm', key, iv);
  const encrypted = Buffer.concat([cipher.update('hello Bob', 'utf8'), cipher.final()]);
  const bytes = Buffer.concat([iv, cipher.getAuthTag(), encrypted]);
  const envelope = {scopeId: scope, originId: origin(alice),
    messageId: '01'.repeat(16), ciphertext: bytes.toString('base64')};
  const push = signed(alice, '/v30/push', {envelope, authorizationGrant: grant(alice, 'push')});
  let response = await post(url, '/v30/push', push);
  assert.equal(response.status, 200);
  assert.equal((await response.json()).accepted, true);
  assert.equal((await post(url, '/v30/push', push)).status, 400);
  const rejectedGrant = grant(alice, 'push');
  const malformed = {...envelope, plaintext: 'must not cross relay'};
  response = await post(url, '/v30/push', signed(alice, '/v30/push',
    {envelope: malformed, authorizationGrant: rejectedGrant}));
  assert.equal(response.status, 400);
  response = await post(url, '/v30/push', signed(bob, '/v30/push',
    {envelope, authorizationGrant: grant(alice, 'push')}));
  assert.equal(response.status, 400);
  response = await fetch(url + '/v30/push', {method: 'POST',
    headers: {'Content-Type': 'application/json'}, body: 'x'.repeat(17000)});
  assert.equal(response.status, 413);
  // Absolute body deadline also bounds a client that never ends its body.
  const began = Date.now();
  await new Promise((resolve, reject) => {
    const slow = request(url + '/v30/push', {method: 'POST',
      headers: {'Content-Type': 'application/json', 'Transfer-Encoding': 'chunked'}});
    slow.on('error', () => resolve());
    slow.on('response', response => { response.resume(); resolve(); });
    slow.write('{');
    const watchdog = setTimeout(() => {slow.destroy(); reject(Error('body_deadline_failed'));}, 8000);
    watchdog.unref();
    slow.on('close', () => clearTimeout(watchdog));
  });
  assert.ok(Date.now() - began < 7500);
  await stop();
  url = await start();
  assert.equal((await post(url, '/v30/push', signed(alice, '/v30/push',
    {envelope, authorizationGrant: rejectedGrant}))).status, 400);
  assert.equal((await post(url, '/v30/push', push)).status, 400); // persisted nonce replay
  const poll = signed(bob, '/v30/poll', {scopeId: scope, consumerOriginId: origin(bob),
    authorizationGrant: grant(bob, 'poll')});
  response = await post(url, '/v30/poll', poll);
  assert.equal(response.status, 200);
  const page = await response.json();
  assert.equal(page.items.length, 1);
  const wire = Buffer.from(page.items[0].ciphertext, 'base64');
  const decipher = createDecipheriv('aes-256-gcm', key, wire.subarray(0, 12));
  decipher.setAuthTag(wire.subarray(12, 28));
  assert.equal(Buffer.concat([decipher.update(wire.subarray(28)), decipher.final()])
    .toString(), 'hello Bob');
  assert.equal(page.nextCursor, 1);
  await stop();

  // A failed durable commit must never report successful admission.
  const failed = {load: async () => null, save: async () => {throw Error('disk failure');}};
  server = await createV30RelayService({issuerPublicKey: issuer.publicKey, storage: failed,
    now: () => nowMs});
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  url = 'http://127.0.0.1:' + server.address().port;
  const nextPush = signed(alice, '/v30/push', {envelope,
    authorizationGrant: grant(alice, 'push')});
  assert.equal((await post(url, '/v30/push', nextPush)).status, 503);
  assert.equal((await post(url, '/v30/push', nextPush)).status, 503);
  await stop();
  console.log('V30 real HTTP two-client ciphertext/restart/replay/failure contract PASS');
} finally {
  if (server?.listening) await stop();
  await rm(directory, {recursive: true, force: true});
}
