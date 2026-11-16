import { createHash } from 'node:crypto';

const HEX = (bytes) => new RegExp('^[0-9a-f]{' + (bytes * 2) + '}$');
const ENVELOPE_FIELDS = new Set(['originId', 'messageId', 'scopeId', 'ciphertext']);
const GRANT_FIELDS = new Set(['scopeId', 'originId', 'rights', 'expiresAt']);
const MAX_ENTRIES = 4096;
const MAX_TTL_MS = 7 * 24 * 60 * 60 * 1000;
const MAX_CIPHERTEXT_BYTES = 65536;
const MAX_POLL_LIMIT = 64;

function hexId(value, bytes, field) {
  if (typeof value !== 'string' || !HEX(bytes).test(value)) {
    throw new Error(field + '_invalid');
  }
  return value;
}

function ciphertext(value, maxBytes) {
  if (typeof value !== 'string' || value.length === 0 ||
      value.length > Math.ceil(maxBytes * 4 / 3) + 4 ||
      !/^[A-Za-z0-9+/]*={0,2}$/.test(value)) {
    throw new Error('ciphertext_invalid');
  }
  const bytes = Buffer.from(value, 'base64');
  if (bytes.length === 0 || bytes.length > maxBytes ||
      bytes.toString('base64') !== value) {
    throw new Error('ciphertext_invalid');
  }
  return bytes;
}

function keyOf(scopeId, originId, messageId) {
  return scopeId + ':' + originId + ':' + messageId;
}

function checkClock(nowMs) {
  if (!Number.isSafeInteger(nowMs) || nowMs < 0) throw new Error('clock_invalid');
}

export class V30RelayCore {
  constructor({ maxEntries = 256, ttlMs = 6 * 60 * 60 * 1000,
                maxCiphertextBytes = 4096 } = {}) {
    if (!Number.isSafeInteger(maxEntries) || maxEntries < 1 ||
        maxEntries > MAX_ENTRIES) throw new Error('max_entries_invalid');
    if (!Number.isSafeInteger(ttlMs) || ttlMs < 1000 || ttlMs > MAX_TTL_MS)
      throw new Error('ttl_invalid');
    if (!Number.isSafeInteger(maxCiphertextBytes) ||
        maxCiphertextBytes < 1 || maxCiphertextBytes > MAX_CIPHERTEXT_BYTES)
      throw new Error('ciphertext_limit_invalid');
    this.maxEntries = maxEntries;
    this.ttlMs = ttlMs;
    this.maxCiphertextBytes = maxCiphertextBytes;
    this.entries = new Map();
    this.nextSequence = 1;
  }

  validateAuthorizationGrant(grant, scopeId, originId, right, nowMs) {
    checkClock(nowMs);
    if (!grant || typeof grant !== 'object' || Array.isArray(grant))
      throw new Error('authorization_grant_missing');
    for (const field of Object.keys(grant)) {
      if (!GRANT_FIELDS.has(field)) throw new Error('authorization_grant_invalid');
    }
    if (grant.scopeId !== scopeId || grant.originId !== originId ||
        !Array.isArray(grant.rights) || grant.rights.length !== 1 ||
        grant.rights[0] !== right || !Number.isSafeInteger(grant.expiresAt) ||
        grant.expiresAt <= nowMs) {
      throw new Error('authorization_grant_mismatch');
    }
    hexId(grant.scopeId, 16, 'grant_scope');
    hexId(grant.originId, 32, 'grant_origin');
    return grant;
  }

  validateEnvelope(envelope) {
    if (!envelope || typeof envelope !== 'object' || Array.isArray(envelope))
      throw new Error('envelope_invalid');
    for (const field of Object.keys(envelope)) {
      if (!ENVELOPE_FIELDS.has(field)) throw new Error('field_not_allowed');
    }
    const originId = hexId(envelope.originId, 32, 'origin');
    const messageId = hexId(envelope.messageId, 16, 'message');
    const scopeId = hexId(envelope.scopeId, 16, 'scope');
    const body = ciphertext(envelope.ciphertext, this.maxCiphertextBytes);
    return Object.freeze({
      originId, messageId, scopeId, ciphertext: envelope.ciphertext,
      ciphertextSha256: createHash('sha256').update(body).digest('hex'),
    });
  }

  purge(nowMs) {
    checkClock(nowMs);
    for (const [key, entry] of this.entries) {
      if (entry.expiresAt <= nowMs) this.entries.delete(key);
    }
  }

  accept(envelope, nowMs, authorizationGrant) {
    checkClock(nowMs);
    const normalized = this.validateEnvelope(envelope);
    this.validateAuthorizationGrant(authorizationGrant, normalized.scopeId,
                                   normalized.originId, 'push', nowMs);
    this.purge(nowMs);
    const key = keyOf(normalized.scopeId, normalized.originId, normalized.messageId);
    const prior = this.entries.get(key);
    if (prior) {
      if (prior.ciphertextSha256 !== normalized.ciphertextSha256)
        return { accepted: false, duplicate: false, error: 'idempotency_conflict' };
      return { accepted: true, duplicate: true, expiresAt: prior.expiresAt,
        sequence: prior.sequence };
    }
    if (this.entries.size >= this.maxEntries)
      return { accepted: false, duplicate: false, error: 'capacity' };
    const expiresAt = nowMs > Number.MAX_SAFE_INTEGER - this.ttlMs
      ? Number.MAX_SAFE_INTEGER : nowMs + this.ttlMs;
    const sequence = this.nextSequence++;
    this.entries.set(key, Object.freeze({
      ...normalized, acceptedAt: nowMs, expiresAt, sequence,
    }));
    return { accepted: true, duplicate: false, expiresAt, sequence };
  }

  poll({ scopeId, consumerOriginId, nowMs, limit = 6, cursor = 0,
          authorizationGrant }) {
    checkClock(nowMs);
    hexId(scopeId, 16, 'scope');
    hexId(consumerOriginId, 32, 'consumer_origin');
    if (!Number.isSafeInteger(limit) || limit < 1 || limit > MAX_POLL_LIMIT)
      throw new Error('limit_invalid');
    if (!Number.isSafeInteger(cursor) || cursor < 0)
      throw new Error('cursor_invalid');
    this.validateAuthorizationGrant(authorizationGrant, scopeId,
                                    consumerOriginId, 'poll', nowMs);
    this.purge(nowMs);
    const out = [];
    let nextCursor = cursor;
    for (const entry of this.entries.values()) {
      if (entry.sequence <= cursor || entry.scopeId !== scopeId ||
          entry.originId === consumerOriginId) continue;
      out.push({ originId: entry.originId, messageId: entry.messageId,
        scopeId: entry.scopeId, ciphertext: entry.ciphertext,
        expiresAt: entry.expiresAt, sequence: entry.sequence });
      nextCursor = entry.sequence;
      if (out.length === limit) break;
    }
    return { items: out, nextCursor };
  }

  size(nowMs) {
    this.purge(nowMs);
    return this.entries.size;
  }
}
