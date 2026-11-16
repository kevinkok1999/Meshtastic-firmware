import { createHash } from 'node:crypto';

const HEX = (bytes) => new RegExp('^[0-9a-f]{' + (bytes * 2) + '}$');
const ALLOWED = new Set(['originId', 'messageId', 'scopeId', 'ciphertext']);

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

export class V30RelayCore {
  constructor({ maxEntries = 256, ttlMs = 6 * 60 * 60 * 1000,
                maxCiphertextBytes = 4096 } = {}) {
    if (!Number.isSafeInteger(maxEntries) || maxEntries < 1) {
      throw new Error('max_entries_invalid');
    }
    if (!Number.isSafeInteger(ttlMs) || ttlMs < 1000) {
      throw new Error('ttl_invalid');
    }
    if (!Number.isSafeInteger(maxCiphertextBytes) ||
        maxCiphertextBytes < 1) {
      throw new Error('ciphertext_limit_invalid');
    }
    this.maxEntries = maxEntries;
    this.ttlMs = ttlMs;
    this.maxCiphertextBytes = maxCiphertextBytes;
    this.entries = new Map();
  }

  validateEnvelope(envelope) {
    if (!envelope || typeof envelope !== 'object' ||
        Array.isArray(envelope)) {
      throw new Error('envelope_invalid');
    }
    for (const field of Object.keys(envelope)) {
      if (!ALLOWED.has(field)) throw new Error('field_not_allowed');
    }
    const originId = hexId(envelope.originId, 32, 'origin');
    const messageId = hexId(envelope.messageId, 16, 'message');
    const scopeId = hexId(envelope.scopeId, 16, 'scope');
    const body = ciphertext(envelope.ciphertext, this.maxCiphertextBytes);
    return Object.freeze({
      originId, messageId, scopeId,
      ciphertext: envelope.ciphertext,
      ciphertextSha256: createHash('sha256').update(body).digest('hex'),
    });
  }

  purge(nowMs) {
    if (!Number.isSafeInteger(nowMs) || nowMs < 0) {
      throw new Error('clock_invalid');
    }
    for (const [key, entry] of this.entries) {
      if (entry.expiresAt <= nowMs) this.entries.delete(key);
    }
  }

  accept(envelope, nowMs) {
    if (!Number.isSafeInteger(nowMs) || nowMs < 0) {
      throw new Error('clock_invalid');
    }
    const normalized = this.validateEnvelope(envelope);
    this.purge(nowMs);
    const key = keyOf(normalized.scopeId, normalized.originId,
                      normalized.messageId);
    const prior = this.entries.get(key);
    if (prior) {
      if (prior.ciphertextSha256 !== normalized.ciphertextSha256) {
        return { accepted: false, duplicate: false, error: 'idempotency_conflict' };
      }
      return { accepted: true, duplicate: true, expiresAt: prior.expiresAt };
    }
    if (this.entries.size >= this.maxEntries) {
      return { accepted: false, duplicate: false, error: 'capacity' };
    }
    const expiresAt = nowMs > Number.MAX_SAFE_INTEGER - this.ttlMs
      ? Number.MAX_SAFE_INTEGER : nowMs + this.ttlMs;
    this.entries.set(key, Object.freeze({
      ...normalized, acceptedAt: nowMs, expiresAt,
    }));
    return { accepted: true, duplicate: false, expiresAt };
  }

  poll(scopeId, consumerOriginId, nowMs, limit = 6) {
    hexId(scopeId, 16, 'scope');
    hexId(consumerOriginId, 32, 'consumer_origin');
    if (!Number.isSafeInteger(limit) || limit < 1 || limit > 64) {
      throw new Error('limit_invalid');
    }
    this.purge(nowMs);
    const out = [];
    for (const entry of this.entries.values()) {
      if (entry.scopeId !== scopeId || entry.originId === consumerOriginId) {
        continue;
      }
      out.push({
        originId: entry.originId,
        messageId: entry.messageId,
        scopeId: entry.scopeId,
        ciphertext: entry.ciphertext,
        expiresAt: entry.expiresAt,
      });
      if (out.length === limit) break;
    }
    return out;
  }

  size(nowMs) {
    this.purge(nowMs);
    return this.entries.size;
  }
}
