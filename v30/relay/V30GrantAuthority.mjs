import { createPublicKey, sign, verify } from 'node:crypto';

const HEX = (bytes) => new RegExp('^[0-9a-f]{' + bytes * 2 + '}$');
const RIGHTS = new Set(['push', 'poll']);
const FIELDS = new Set(['scopeId', 'originId', 'rights', 'expiresAt', 'nonce', 'signature']);
const MAX_TTL_MS = 7 * 24 * 60 * 60 * 1000;
const MAX_REPLAY = 4096;

function id(value, bytes, field) {
  if (typeof value !== 'string' || !HEX(bytes).test(value))
    throw new Error(field + '_invalid');
  return value;
}
function clock(nowMs) {
  if (!Number.isSafeInteger(nowMs) || nowMs < 0) throw new Error('clock_invalid');
}
function canonical(grant) {
  return Buffer.from([
    'V30-G4-GRANT-V1', grant.scopeId, grant.originId,
    grant.rights.join(','), String(grant.expiresAt), grant.nonce,
  ].join('|'), 'ascii');
}
function signature(value) {
  if (typeof value !== 'string' || !/^[A-Za-z0-9_-]{86}$/.test(value))
    throw new Error('signature_invalid');
  const bytes = Buffer.from(value, 'base64url');
  if (bytes.length !== 64) throw new Error('signature_invalid');
  return bytes;
}

export class V30GrantAuthority {
  constructor({ privateKey, issuerPublicKey, maxReplay = MAX_REPLAY } = {}) {
    if (!issuerPublicKey && !privateKey) throw new Error('issuer_key_missing');
    this.privateKey = privateKey || null;
    this.issuerPublicKey = issuerPublicKey ||
      createPublicKey(privateKey);
    if (!Number.isSafeInteger(maxReplay) || maxReplay < 1 ||
        maxReplay > MAX_REPLAY) throw new Error('replay_limit_invalid');
    this.maxReplay = maxReplay;
    this.replay = new Map();
  }

  issueGrant({ scopeId, originId, right, nowMs, ttlMs = 60000, nonce }) {
    if (!this.privateKey) throw new Error('issuer_private_key_missing');
    clock(nowMs);
    id(scopeId, 16, 'scope');
    id(originId, 32, 'origin');
    if (!RIGHTS.has(right)) throw new Error('right_invalid');
    if (!Number.isSafeInteger(ttlMs) || ttlMs < 1000 || ttlMs > MAX_TTL_MS)
      throw new Error('ttl_invalid');
    id(nonce, 16, 'nonce');
    const expiresAt = nowMs > Number.MAX_SAFE_INTEGER - ttlMs
      ? Number.MAX_SAFE_INTEGER : nowMs + ttlMs;
    const unsigned = {scopeId, originId, rights: [right], expiresAt, nonce};
    const sig = sign(null, canonical(unsigned), this.privateKey).toString('base64url');
    return Object.freeze({...unsigned, signature: sig});
  }

  verifyGrant(grant, { scopeId, originId, right, nowMs } = {}) {
    clock(nowMs);
    if (!grant || typeof grant !== 'object' || Array.isArray(grant))
      throw new Error('grant_missing');
    for (const field of Object.keys(grant))
      if (!FIELDS.has(field)) throw new Error('grant_invalid');
    id(grant.scopeId, 16, 'scope');
    id(grant.originId, 32, 'origin');
    id(grant.nonce, 16, 'nonce');
    if (!Array.isArray(grant.rights) || grant.rights.length !== 1 ||
        !RIGHTS.has(grant.rights[0]) || !Number.isSafeInteger(grant.expiresAt) ||
        grant.expiresAt <= nowMs) throw new Error('grant_expired_or_invalid');
    signature(grant.signature);
    if (scopeId !== grant.scopeId || originId !== grant.originId ||
        right !== grant.rights[0]) throw new Error('grant_scope_mismatch');
    if (!verify(null, canonical(grant), this.issuerPublicKey,
                signature(grant.signature))) throw new Error('grant_signature_invalid');
    for (const [nonce, expiresAt] of this.replay)
      if (expiresAt <= nowMs) this.replay.delete(nonce);
    if (this.replay.has(grant.nonce)) throw new Error('grant_replay');
    if (this.replay.size >= this.maxReplay) throw new Error('grant_replay_capacity');
    this.replay.set(grant.nonce, grant.expiresAt);
    return Object.freeze({
      scopeId: grant.scopeId, originId: grant.originId,
      rights: [...grant.rights], expiresAt: grant.expiresAt,
    });
  }
}
