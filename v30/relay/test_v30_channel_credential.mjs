import assert from 'node:assert/strict';
import { test } from 'node:test';
import { generateKeyPairSync, createPublicKey, verify } from 'node:crypto';

const key='00112233445566778899aabbccddeeff';
const expectedScope='fe75622e5745852ed4f2d305f09e95d5';
const expectedVerifier='6718f1f877ba903022b122dd80bdd790702ffd82236fe73a32b5b8c27b7f4b4e';
const raw=key=>key.export({format:'der',type:'spki'}).subarray(-32).toString('hex');
const publicKey=value=>createPublicKey({key:Buffer.concat([Buffer.from('302a300506032b6570032100','hex'),Buffer.from(value,'hex')]),format:'der',type:'spki'});
const valid=(key,message,signature)=>verify(null,Buffer.from(message,'ascii'),key,Buffer.from(signature,'base64url'));
async function factory(){
  let module;try{module=await import('./V30ChannelCredential.mjs');}catch(error){if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;assert.fail('reference credential client has not been implemented');}
  return module.V30ChannelCredential;
}

test('exact 32-hex input yields independent existing-scope and membership public-key vectors',async()=>{
  const Credential=await factory(), device=generateKeyPairSync('ed25519');
  for(const input of [key,key.toUpperCase(),' '+key+' ']){
    const credential=new Credential(input,device.privateKey);
    assert.deepEqual(credential.publicBinding(),{scopeId:expectedScope,membershipPublicKey:expectedVerifier});
    assert.equal(JSON.stringify(credential),'{}');
  }
  for(const input of ['hunter2',key+'00',key.slice(2),key.slice(0,10)+' '+key.slice(11),null,'z'.repeat(32)]) assert.throws(()=>new Credential(input,device.privateKey));
  assert.throws(()=>new Credential(key,device.publicKey));
});

test('begin and finish proofs are bound to local origin/scope/right and server challenge',async()=>{
  const Credential=await factory(), device=generateKeyPairSync('ed25519');
  const credential=new Credential(key,device.privateKey), originId=raw(device.publicKey);
  const b=credential.begin('push');
  assert.equal(b.scopeId,expectedScope);assert.equal(b.originId,originId);assert.match(b.clientNonce,/^[0-9a-f]{32}$/);
  assert.notEqual(credential.begin('push').clientNonce,b.clientNonce);
  const beginMessage=['V30-G4-BEGIN-V1',expectedScope,originId,'push',b.clientNonce].join('|');
  assert.ok(valid(device.publicKey,beginMessage,b.deviceSignature));
  assert.ok(valid(publicKey(expectedVerifier),beginMessage,b.membershipSignature));
  const c={scopeId:expectedScope,originId,right:'push',nonce:'12'.repeat(16),issuedAt:1000,expiresAt:61000,challengeExpiresAt:16000};
  const body=credential.finish(c,'push'), finishMessage=['V30-G4-ISSUE-V1',expectedScope,originId,'push','61000',c.nonce].join('|');
  assert.ok(valid(device.publicKey,finishMessage,body.deviceSignature));
  assert.ok(valid(publicKey(expectedVerifier),finishMessage,body.membershipSignature));
  assert.ok(!JSON.stringify([b,body]).includes(key));
  assert.throws(()=>credential.begin('admin'));
  for(const challenge of [{...c,right:'poll'},{...c,scopeId:'00'.repeat(16)},{...c,originId:'00'.repeat(32)},{...c,nonce:'xx'},{...c,expiresAt:62000},{...c,challengeExpiresAt:17000},{...c,issuedAt:-1},{...c,extra:true}])assert.throws(()=>credential.finish(challenge,'push'));
});
