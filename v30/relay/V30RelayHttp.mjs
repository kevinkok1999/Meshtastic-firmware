import { createServer } from 'node:http';
import { createHash, createPublicKey, verify } from 'node:crypto';
import { V30RelayCore } from './V30RelayCore.mjs';
import { V30GrantAuthority } from './V30GrantAuthority.mjs';
import { V30RelayAdapter } from './V30RelayAdapter.mjs';

const MAX_BODY = 16384;
const SPKI = Buffer.from('302a300506032b6570032100', 'hex');

export function canonicalHttpRequest(path, request) {
  // Explicit tuple construction avoids dependence on JSON property order.
  const g = request.authorizationGrant;
  const grant = [g?.scopeId, g?.originId, g?.rights, g?.expiresAt, g?.nonce, g?.signature];
  const operation = path === '/v30/push'
    ? [request.envelope?.scopeId, request.envelope?.originId,
       request.envelope?.messageId, request.envelope?.ciphertext]
    : [request.scopeId, request.consumerOriginId, request.limit ?? 6, request.cursor ?? 0];
  return Buffer.from('V30-HTTP-V1|' + path + '|' +
    createHash('sha256').update(JSON.stringify([grant, operation])).digest('hex'));
}

function shape(body, path) {
  if (!body || typeof body !== 'object' || Array.isArray(body)) throw Error('body_invalid');
  const fields = new Set(path === '/v30/push'
    ? ['authorizationGrant', 'envelope', 'deviceSignature']
    : ['authorizationGrant', 'scopeId', 'consumerOriginId', 'limit', 'cursor', 'deviceSignature']);
  if (Object.keys(body).some(k => !fields.has(k))) throw Error('field_not_allowed');
  const origin = path === '/v30/push' ? body.envelope?.originId : body.consumerOriginId;
  if (typeof origin !== 'string' || !/^[0-9a-f]{64}$/.test(origin))
    throw Error('origin_invalid');
  if (typeof body.deviceSignature !== 'string' ||
      !/^[A-Za-z0-9_-]{86}$/.test(body.deviceSignature)) throw Error('proof_invalid');
  const key = createPublicKey({key: Buffer.concat([SPKI, Buffer.from(origin, 'hex')]),
    format: 'der', type: 'spki'});
  if (!verify(null, canonicalHttpRequest(path, body), key,
              Buffer.from(body.deviceSignature, 'base64url'))) throw Error('proof_invalid');
}

async function readBody(req) {
  if (req.headers['content-type'] !== 'application/json') throw Error('content_type_invalid');
  const chunks = [];
  let count = 0;
  for await (const chunk of req) {
    count += chunk.length;
    if (count > MAX_BODY) throw Error('body_too_large');
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString('utf8'));
}

function exportState(core, authority) {
  return {version: 1, entries: [...core.entries], nextSequence: core.nextSequence,
    replay: [...authority.replay]};
}

function restoreState(state, core, authority) {
  if (state === null) return;
  if (!state || state.version !== 1 || !Array.isArray(state.entries) ||
      !Array.isArray(state.replay) || state.entries.length > core.maxEntries ||
      state.replay.length > authority.maxReplay ||
      !Number.isSafeInteger(state.nextSequence) || state.nextSequence < 1)
    throw Error('storage_state_invalid');
  const entries = new Map();
  let previousSequence = 0;
  for (const pair of state.entries) {
    if (!Array.isArray(pair) || pair.length !== 2) throw Error('storage_state_invalid');
    const [key, value] = pair;
    const normalized = core.validateEnvelope({
      originId: value?.originId, scopeId: value?.scopeId,
      messageId: value?.messageId, ciphertext: value?.ciphertext,
    });
    if (key !== [normalized.scopeId, normalized.originId, normalized.messageId].join(':') ||
        entries.has(key) || normalized.ciphertextSha256 !== value.ciphertextSha256 ||
        !Number.isSafeInteger(value.sequence) || value.sequence <= previousSequence ||
        value.sequence >= state.nextSequence ||
        !Number.isSafeInteger(value.acceptedAt) || value.acceptedAt < 0 ||
        !Number.isSafeInteger(value.expiresAt) || value.expiresAt <= value.acceptedAt)
      throw Error('storage_state_invalid');
    previousSequence = value.sequence;
    entries.set(key, Object.freeze({...normalized, acceptedAt: value.acceptedAt,
      expiresAt: value.expiresAt, sequence: value.sequence}));
  }
  const replay = new Map();
  for (const pair of state.replay) {
    if (!Array.isArray(pair) || pair.length !== 2 ||
        typeof pair[0] !== 'string' || !/^[0-9a-f]{32}$/.test(pair[0]) ||
        replay.has(pair[0]) || !Number.isSafeInteger(pair[1]) || pair[1] < 0)
      throw Error('storage_state_invalid');
    replay.set(...pair);
  }
  core.entries = entries;
  core.nextSequence = state.nextSequence;
  authority.replay = replay;
}

// storage.load()/save(state) must be atomic and durable before save resolves.
// No grant issuer or network grant-minting route is exposed by this service.
export async function createV30RelayService({issuerPublicKey, storage,
                                            now = Date.now} = {}) {
  if (!storage || typeof storage.load !== 'function' || typeof storage.save !== 'function')
    throw Error('storage_required');
  const core = new V30RelayCore();
  const authority = new V30GrantAuthority({issuerPublicKey});
  const adapter = new V30RelayAdapter(core, authority);
  restoreState(await storage.load(), core, authority);
  let queue = Promise.resolve();
  let pending = 0;
  let poisoned = false;
  const server = createServer(async (req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('Content-Type', 'application/json');
    const send = (status, value) => {
      if (!res.destroyed) { res.statusCode = status; res.end(JSON.stringify(value)); }
    };
    if (req.method !== 'POST' || !['/v30/push', '/v30/poll'].includes(req.url)) {
      req.resume(); return send(404, {error: 'route_invalid'});
    }
    if (pending >= 16 || poisoned) { req.resume(); return send(503, {error: 'unavailable'}); }
    ++pending;
    const bodyDeadline = setTimeout(() => req.destroy(), 5000);
    bodyDeadline.unref();
    try {
      const body = await readBody(req);
      clearTimeout(bodyDeadline);
      shape(body, req.url);
      const operation = queue.then(async () => {
        if (poisoned) throw Error('storage_unavailable');
        const nowMs = now();
        let result, rejected;
        try {
          result = req.url === '/v30/push'
            ? adapter.accept(body.envelope, body.authorizationGrant, nowMs)
            : adapter.poll({...body, nowMs});
        } catch (error) {
          rejected = error;
        }
        // Verification may consume a grant nonce before core validation fails.
        // Commit that denial too; otherwise restart could resurrect that nonce.
        try { await storage.save(exportState(core, authority)); }
        catch { poisoned = true; throw Error('storage_unavailable'); }
        if (rejected) throw rejected;
        return result;
      });
      queue = operation.catch(() => {});
      const result = await operation;
      send(result.accepted === false ? 409 : 200, result);
    } catch (error) {
      const code = error.message;
      send(code === 'storage_unavailable' ? 503 : code === 'body_too_large' ? 413 : 400,
        {error: code === 'body_too_large' ? code : code === 'storage_unavailable'
          ? code : 'request_rejected'});
    } finally { clearTimeout(bodyDeadline); --pending; }
  });
  server.requestTimeout = 5000;
  server.headersTimeout = 5000;
  server.timeout = 5000;
  server.maxHeadersCount = 32;
  return server;
}
