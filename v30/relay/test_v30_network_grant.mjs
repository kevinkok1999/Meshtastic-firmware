import assert from 'node:assert/strict';
import { test } from 'node:test';
import { generateKeyPairSync, createPrivateKey, createPublicKey, createHmac,
  createCipheriv, createDecipheriv, randomBytes, sign } from 'node:crypto';
import { once } from 'node:events';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createV30RelayService, canonicalHttpRequest } from './V30RelayHttp.mjs';
import { V30FileStorage } from './V30FileStorage.mjs';
import { V30ChannelCredential } from './V30ChannelCredential.mjs';

// Test-only high-entropy-shaped value; independent golden vector calculated
// against the existing Python HMAC contract, not by the new server.
const keyHex = '00112233445566778899aabbccddeeff';
const otherHex = 'ffeeddccbbaa99887766554433221100';
const expectedScope = 'fe75622e5745852ed4f2d305f09e95d5';
const raw = key => key.export({format:'der',type:'spki'}).subarray(-32).toString('hex');
const origin = pair => raw(pair.publicKey);
const sig = (key, bytes) => sign(null, bytes, key).toString('base64url');
function member(hex) {
  const key = Buffer.from(hex, 'hex');
  const scope = createHmac('sha256', key).update('MOG27-CH-INBOX').digest('hex').slice(0,32);
  const seed = createHmac('sha256', key).update('MOG30-CH-MEMBER-ED25519').digest();
  const privateKey = createPrivateKey({key:Buffer.concat([Buffer.from('302e020100300506032b657004220420','hex'),seed]),format:'der',type:'pkcs8'});
  seed.fill(0);
  return {scope, privateKey, publicKey:raw(createPublicKey(privateKey)),
    aes:createHmac('sha256',key).update('MOG30-CH-AES-GCM').digest()};
}
const initial = member(keyHex);
function begin(device, group, right, clientNonce = randomBytes(16).toString('hex'), scopeId = group.scope) {
  const originId=origin(device);
  const bytes=Buffer.from(['V30-G4-BEGIN-V1',scopeId,originId,right,clientNonce].join('|'),'ascii');
  return {scopeId,originId,right,clientNonce,deviceSignature:sig(device.privateKey,bytes),membershipSignature:sig(group.privateKey,bytes)};
}
function finish(device, group, challenge) {
  const {scopeId,originId,right,nonce,expiresAt}=challenge;
  const bytes=Buffer.from(['V30-G4-ISSUE-V1',scopeId,originId,right,String(expiresAt),nonce].join('|'),'ascii');
  return {scopeId,originId,right,nonce,expiresAt,deviceSignature:sig(device.privateKey,bytes),membershipSignature:sig(group.privateKey,bytes)};
}
async function fixture(t, opts={}) {
  const issuer=generateKeyPairSync('ed25519');
  const directory=await mkdtemp(join(tmpdir(),'v30-grant-net-'));
  const file=join(directory,'state.json');
  const storage=new V30FileStorage(file);
  let clock=100000, server, url;
  const start=async()=>{
    server=await createV30RelayService({issuerPublicKey:issuer.publicKey,storage,now:()=>clock,
      ...(!opts.disabled?{grantIssuer:{privateKey:issuer.privateKey,memberships:opts.memberships??new Map([[initial.scope,initial.publicKey]]),maxChallenges:opts.maxChallenges??8,...(opts.maxPerScope===undefined?{}:{maxPerScope:opts.maxPerScope})}}:{})});
    server.listen(0,'127.0.0.1'); await once(server,'listening'); url='http://127.0.0.1:'+server.address().port;
  };
  const stop=async()=>{if(server?.listening){server.closeAllConnections();await new Promise((resolve,reject)=>server.close(e=>e?reject(e):resolve()));}};
  t.after(async()=>{await stop();await rm(directory,{recursive:true,force:true});});
  await start();
  const post=async(path,body)=>fetch(url+path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const challenge=async(device,group,right)=>{
    const response=await post('/v30/grants/challenge',begin(device,group,right));
    assert.equal(response.status,200); return response.json();
  };
  const grant=async(device,group,right)=>{
    const c=await challenge(device,group,right);const response=await post('/v30/grants/issue',finish(device,group,c));
    assert.equal(response.status,200);return response.json();
  };
  return {post,challenge,grant,file,setClock:n=>{clock=n;},restart:async()=>{await stop();await start();}};
}
const operation=(device,path,body)=>({...body,deviceSignature:sig(device.privateKey,canonicalHttpRequest(path,body))});

test('two independently keyed devices use the same legacy channel scope and exchange opaque ciphertext through real TCP',async t=>{
  const f=await fixture(t), alice=generateKeyPairSync('ed25519'), bob=generateKeyPairSync('ed25519');
  const a=member(keyHex), b=member(keyHex.toUpperCase());
  const aliceCredential=new V30ChannelCredential(keyHex,alice.privateKey),bobCredential=new V30ChannelCredential(keyHex.toUpperCase(),bob.privateKey);
  const getGrant=async(credential,right)=>{
    const challenge=await f.post('/v30/grants/challenge',credential.begin(right));assert.equal(challenge.status,200);
    const response=await f.post('/v30/grants/issue',credential.finish(await challenge.json(),right));assert.equal(response.status,200);return response.json();
  };
  assert.equal(a.scope,expectedScope); assert.equal(b.scope,expectedScope);
  assert.notEqual(origin(alice),origin(bob));
  const messageId='21'.repeat(16), envelope={scopeId:a.scope,originId:origin(alice),messageId};
  const iv=randomBytes(12), cipher=createCipheriv('aes-256-gcm',a.aes,iv);
  cipher.setAAD(Buffer.from([a.scope,origin(alice),messageId].join('|'),'ascii'));
  const encrypted=Buffer.concat([cipher.update('V30 test private hello','utf8'),cipher.final()]);
  envelope.ciphertext=Buffer.concat([iv,cipher.getAuthTag(),encrypted]).toString('base64');
  const push=operation(alice,'/v30/push',{envelope,authorizationGrant:await getGrant(aliceCredential,'push')});
  const response=await f.post('/v30/push',push);assert.equal(response.status,200);assert.equal((await response.json()).accepted,true);
  const persisted=await readFile(f.file,'utf8');
  assert.ok(!persisted.includes(keyHex));assert.ok(!persisted.includes('V30 test private hello'));assert.ok(persisted.includes(envelope.ciphertext));
  await f.restart(); assert.equal((await f.post('/v30/push',push)).status,400);
  const poll=operation(bob,'/v30/poll',{scopeId:b.scope,consumerOriginId:origin(bob),authorizationGrant:await getGrant(bobCredential,'poll')});
  const received=await f.post('/v30/poll',poll);assert.equal(received.status,200);
  const page=await received.json();assert.equal(page.items.length,1);
  const item=page.items[0], bytes=Buffer.from(item.ciphertext,'base64');
  const decipher=createDecipheriv('aes-256-gcm',b.aes,bytes.subarray(0,12));
  decipher.setAAD(Buffer.from([item.scopeId,item.originId,item.messageId].join('|'),'ascii'));decipher.setAuthTag(bytes.subarray(12,28));
  assert.equal(Buffer.concat([decipher.update(bytes.subarray(28)),decipher.final()]).toString(),'V30 test private hello');
});

test('unregistered or incorrect membership and invalid device proof cannot reserve or receive grants',async t=>{
  const f=await fixture(t), alice=generateKeyPairSync('ed25519'), attacker=generateKeyPairSync('ed25519'), other=member(otherHex);
  assert.equal((await f.post('/v30/grants/challenge',begin(attacker,other,'push'))).status,400);
  assert.equal((await f.post('/v30/grants/challenge',begin(attacker,other,'push',undefined,initial.scope))).status,400);
  const request=begin(alice,initial,'poll');request.deviceSignature=sig(attacker.privateKey,Buffer.from('not Alice'));
  assert.equal((await f.post('/v30/grants/challenge',request)).status,400);
  const c=await f.challenge(alice,initial,'push');
  assert.equal((await f.post('/v30/grants/issue',finish(attacker,initial,c))).status,400);
  assert.equal((await f.post('/v30/grants/issue',finish(alice,other,c))).status,400);
  assert.equal((await f.post('/v30/grants/issue',finish(alice,initial,{...c,right:'poll'}))).status,400);
  assert.equal((await f.post('/v30/grants/issue',finish(alice,initial,{...c,expiresAt:c.expiresAt+1}))).status,400);
  assert.equal((await f.post('/v30/grants/issue',{...finish(alice,initial,c),channelKey:keyHex})).status,400);
  const legitimate=await f.post('/v30/grants/issue',finish(alice,initial,c));assert.equal(legitimate.status,200);
  const grant=await legitimate.json(); assert.equal(grant.scopeId,initial.scope);assert.equal(grant.originId,origin(alice));assert.deepEqual(grant.rights,['push']);
  assert.equal((await f.post('/v30/grants/issue',finish(alice,initial,c))).status,400);
});

test('challenge capacity, one outstanding scope/device/right, expiry and restart fail closed',async t=>{
  const f=await fixture(t,{maxChallenges:1}), alice=generateKeyPairSync('ed25519'), bob=generateKeyPairSync('ed25519');
  const request=begin(alice,initial,'poll');
  const response=await f.post('/v30/grants/challenge',request);assert.equal(response.status,200);const c=await response.json();
  assert.equal(c.challengeExpiresAt,115000);assert.equal(c.expiresAt,160000);
  assert.equal((await f.post('/v30/grants/challenge',request)).status,400);
  assert.equal((await f.post('/v30/grants/challenge',begin(bob,initial,'poll'))).status,400);
  f.setClock(c.challengeExpiresAt);
  assert.equal((await f.post('/v30/grants/issue',finish(alice,initial,c))).status,400);
  const newer=await f.challenge(bob,initial,'poll');await f.restart();
  assert.equal((await f.post('/v30/grants/issue',finish(bob,initial,newer))).status,400);
  const fresh=await f.challenge(bob,initial,'poll');f.setClock(1);
  assert.equal((await f.post('/v30/grants/issue',finish(bob,initial,fresh))).status,400);
});

test('issuer is opt-in and the default relay exposes no grant routes',async t=>{
  const f=await fixture(t,{disabled:true}), alice=generateKeyPairSync('ed25519');
  assert.equal((await f.post('/v30/grants/challenge',begin(alice,initial,'push'))).status,404);
});

test('one authorized scope cannot saturate every challenge slot with new devices',async t=>{
  const other=member(otherHex), f=await fixture(t,{maxChallenges:2,maxPerScope:1,memberships:new Map([[initial.scope,initial.publicKey],[other.scope,other.publicKey]])});
  const alice=generateKeyPairSync('ed25519'), second=generateKeyPairSync('ed25519'), bob=generateKeyPairSync('ed25519');
  await f.challenge(alice,initial,'push');
  assert.equal((await f.post('/v30/grants/challenge',begin(second,initial,'push'))).status,400);
  const c=await f.challenge(bob,other,'poll');
  assert.equal((await f.post('/v30/grants/issue',finish(bob,other,c))).status,200);
});
