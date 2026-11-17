import { createPublicKey, randomBytes, verify } from 'node:crypto';
import { V30GrantAuthority, canonicalIssueChallenge } from './V30GrantAuthority.mjs';

const SPKI = Buffer.from('302a300506032b6570032100','hex');
const CHALLENGE_TTL = 15000;
const GRANT_TTL = 60000;
const MAX_PENDING = 256;
const MAX_MEMBERSHIPS = 4096;
const RIGHT = new Set(['push','poll']);
const BEGIN_FIELDS = new Set(['scopeId','originId','right','clientNonce','deviceSignature','membershipSignature']);
const ISSUE_FIELDS = new Set(['scopeId','originId','right','nonce','expiresAt','deviceSignature','membershipSignature']);

function hex(value, n) {
  if (typeof value !== 'string' || !new RegExp('^[0-9a-f]{'+n*2+'}$').test(value)) throw Error('identity_invalid');
  return value;
}
function publicKey(value) {
  return createPublicKey({key:Buffer.concat([SPKI,Buffer.from(hex(value,32),'hex')]),format:'der',type:'spki'});
}
function checkedClock(nowMs) {
  if (!Number.isSafeInteger(nowMs) || nowMs < 0 || nowMs > Number.MAX_SAFE_INTEGER-GRANT_TTL) throw Error('clock_invalid');
}
function shape(body, fields) {
  if (!body || typeof body !== 'object' || Array.isArray(body) ||
      Object.keys(body).length !== fields.size || Object.keys(body).some(k=>!fields.has(k))) throw Error('request_invalid');
  hex(body.scopeId,16); hex(body.originId,32);
  if (!RIGHT.has(body.right)) throw Error('right_invalid');
}
function checkSignature(key, bytes, value) {
  if (typeof value !== 'string' || !/^[A-Za-z0-9_-]{86}$/.test(value)) throw Error('proof_invalid');
  const signature=Buffer.from(value,'base64url');
  if (signature.length!==64 || signature.toString('base64url')!==value || !verify(null,bytes,key,signature)) throw Error('proof_invalid');
}

export function canonicalBeginChallenge({scopeId,originId,right,clientNonce}) {
  return Buffer.from(['V30-G4-BEGIN-V1',scopeId,originId,right,clientNonce].join('|'),'ascii');
}

// Only configured public verifiers are accepted. A network request cannot
// register or replace a scope or submit the private channel key.
export class V30NetworkGrantIssuer {
  constructor({privateKey, issuerPublicKey, memberships, maxChallenges=MAX_PENDING,
               maxPerScope=Math.min(16,maxChallenges)}) {
    if (privateKey?.type!=='private' || privateKey.asymmetricKeyType!=='ed25519' ||
        issuerPublicKey?.type!=='public' || issuerPublicKey.asymmetricKeyType!=='ed25519' ||
        !createPublicKey(privateKey).export({format:'der',type:'spki'}).equals(issuerPublicKey.export({format:'der',type:'spki'}))) throw Error('issuer_key_mismatch');
    if (!(memberships instanceof Map) || memberships.size<1 || memberships.size>MAX_MEMBERSHIPS) throw Error('membership_registry_invalid');
    if (!Number.isSafeInteger(maxChallenges) || maxChallenges<1 || maxChallenges>MAX_PENDING) throw Error('challenge_limit_invalid');
    if (!Number.isSafeInteger(maxPerScope) || maxPerScope<1 || maxPerScope>maxChallenges) throw Error('scope_limit_invalid');
    this.memberships=new Map([...memberships].map(([scope,key])=>[hex(scope,16),publicKey(key)]));
    this.authority=new V30GrantAuthority({privateKey,issuerPublicKey});
    this.pending=new Map();
    this.maxChallenges=maxChallenges;
    this.maxPerScope=maxPerScope;
  }

  begin(body, nowMs) {
    checkedClock(nowMs);shape(body,BEGIN_FIELDS);hex(body.clientNonce,16);
    const membership=this.memberships.get(body.scopeId);
    if(!membership) throw Error('membership_unknown');
    const bytes=canonicalBeginChallenge(body);
    checkSignature(publicKey(body.originId),bytes,body.deviceSignature);
    checkSignature(membership,bytes,body.membershipSignature);
    for(const [nonce,c] of this.pending) {
      if(c.challengeExpiresAt<=nowMs || c.issuedAt>nowMs) this.pending.delete(nonce);
    }
    let scopeCount=0;
    for(const c of this.pending.values()) {
      if(c.scopeId!==body.scopeId) continue;
      scopeCount++;
      if(c.originId===body.originId && c.right===body.right) throw Error('challenge_pending');
    }
    if(this.pending.size>=this.maxChallenges || scopeCount>=this.maxPerScope) throw Error('challenge_capacity');
    const nonce=randomBytes(16).toString('hex');
    if(this.pending.has(nonce)) throw Error('challenge_nonce_collision');
    const c=Object.freeze({scopeId:body.scopeId,originId:body.originId,right:body.right,nonce,
      issuedAt:nowMs,expiresAt:nowMs+GRANT_TTL,challengeExpiresAt:nowMs+CHALLENGE_TTL});
    this.pending.set(nonce,c);
    return c;
  }

  issue(body, nowMs) {
    checkedClock(nowMs);shape(body,ISSUE_FIELDS);hex(body.nonce,16);
    const c=this.pending.get(body.nonce);
    if(!c || nowMs<c.issuedAt || nowMs>=c.challengeExpiresAt) {
      if(c) this.pending.delete(body.nonce);
      throw Error('challenge_unavailable');
    }
    if(body.scopeId!==c.scopeId || body.originId!==c.originId || body.right!==c.right || body.expiresAt!==c.expiresAt) throw Error('challenge_mismatch');
    const membership=this.memberships.get(c.scopeId);
    if(!membership) throw Error('membership_unknown');
    const bytes=canonicalIssueChallenge(c);
    checkSignature(publicKey(c.originId),bytes,body.deviceSignature);
    checkSignature(membership,bytes,body.membershipSignature);
    // A valid challenge is single-use, even if authority issuance fails.
    this.pending.delete(c.nonce);
    return this.authority.issueGrantForDevice({scopeId:c.scopeId,originId:c.originId,right:c.right,
      nowMs:c.issuedAt,ttlMs:GRANT_TTL,nonce:c.nonce,deviceSignature:body.deviceSignature,
      authorize:q=>q.scopeId===c.scopeId && q.originId===c.originId && q.right===c.right && q.expiresAt===c.expiresAt});
  }
}
