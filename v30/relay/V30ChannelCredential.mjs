import { createHmac, createPrivateKey, createPublicKey, randomBytes, sign } from 'node:crypto';
import { canonicalIssueChallenge } from './V30GrantAuthority.mjs';
import { canonicalBeginChallenge } from './V30NetworkGrantIssuer.mjs';

const PKCS8=Buffer.from('302e020100300506032b657004220420','hex');
const RIGHT=new Set(['push','poll']);
const CHALLENGE_FIELDS=new Set(['scopeId','originId','right','nonce','issuedAt','expiresAt','challengeExpiresAt']);
const raw=key=>key.export({format:'der',type:'spki'}).subarray(-32).toString('hex');
const signature=(key,bytes)=>sign(null,bytes,key).toString('base64url');

// Development reference client. Never send or serialize channelHex or either
// private key. Scope matches the native V27/V28 capability exactly; the
// additional membership seed is independently domain-separated.
export class V30ChannelCredential {
  #scopeId;
  #originId;
  #deviceKey;
  #memberKey;
  #membershipPublicKey;

  constructor(channelHex, deviceKey) {
    if(typeof channelHex!=='string' || !/^[0-9a-fA-F]{32}$/.test(channelHex.trim())) throw Error('channel_key_invalid');
    if(deviceKey?.type!=='private' || deviceKey.asymmetricKeyType!=='ed25519') throw Error('device_key_invalid');
    const key=Buffer.from(channelHex.trim(),'hex');
    const seed=createHmac('sha256',key).update('MOG30-CH-MEMBER-ED25519','ascii').digest();
    const encoded=Buffer.concat([PKCS8,seed]);
    try {
      this.#scopeId=createHmac('sha256',key).update('MOG27-CH-INBOX','ascii').digest('hex').slice(0,32);
      this.#memberKey=createPrivateKey({key:encoded,format:'der',type:'pkcs8'});
      this.#membershipPublicKey=raw(createPublicKey(this.#memberKey));
    } finally {key.fill(0);seed.fill(0);encoded.fill(0);}
    this.#deviceKey=deviceKey;
    this.#originId=raw(createPublicKey(deviceKey));
  }
  publicBinding() {
    return Object.freeze({scopeId:this.#scopeId,membershipPublicKey:this.#membershipPublicKey});
  }
  begin(right) {
    if(!RIGHT.has(right)) throw Error('right_invalid');
    const request={scopeId:this.#scopeId,originId:this.#originId,right,clientNonce:randomBytes(16).toString('hex')};
    const bytes=canonicalBeginChallenge(request);
    return {...request,deviceSignature:signature(this.#deviceKey,bytes),membershipSignature:signature(this.#memberKey,bytes)};
  }
  finish(challenge, right) {
    if(!RIGHT.has(right) || !challenge || typeof challenge!=='object' || Array.isArray(challenge) ||
      Object.keys(challenge).length!==CHALLENGE_FIELDS.size || Object.keys(challenge).some(k=>!CHALLENGE_FIELDS.has(k)) ||
      challenge.scopeId!==this.#scopeId || challenge.originId!==this.#originId || challenge.right!==right ||
      typeof challenge.nonce!=='string' || !/^[0-9a-f]{32}$/.test(challenge.nonce) ||
      !Number.isSafeInteger(challenge.issuedAt) || challenge.issuedAt<0 || challenge.issuedAt>Number.MAX_SAFE_INTEGER-60000 ||
      challenge.expiresAt!==challenge.issuedAt+60000 || challenge.challengeExpiresAt!==challenge.issuedAt+15000) throw Error('challenge_invalid');
    const request={scopeId:challenge.scopeId,originId:challenge.originId,right,nonce:challenge.nonce,expiresAt:challenge.expiresAt};
    const bytes=canonicalIssueChallenge(request);
    return {...request,deviceSignature:signature(this.#deviceKey,bytes),membershipSignature:signature(this.#memberKey,bytes)};
  }
}
