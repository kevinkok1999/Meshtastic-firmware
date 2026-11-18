import assert from 'node:assert/strict';
import { test } from 'node:test';
import { execFileSync } from 'node:child_process';
import { createCipheriv, createDecipheriv, createHmac, generateKeyPairSync,
  randomBytes, sign } from 'node:crypto';
import { once } from 'node:events';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { request as httpRequest } from 'node:http';
import { request as httpsRequest } from 'node:https';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { V30ChannelCredential } from './V30ChannelCredential.mjs';
import { V30FileStorage } from './V30FileStorage.mjs';
import { canonicalHttpRequest, createV30RelayService } from './V30RelayHttp.mjs';

const channelKey = '00112233445566778899aabbccddeeff'; // test-only key
const scopeId = 'fe75622e5745852ed4f2d305f09e95d5'; // independent known vector
const raw = key => key.export({format:'der',type:'spki'}).subarray(-32).toString('hex');
const signRequest = (device,path,body) => ({...body,
  deviceSignature:sign(null,canonicalHttpRequest(path,body),device.privateKey).toString('base64url')});

async function fixture(t) {
  const directory = await mkdtemp(join(tmpdir(),'v30-tls-test-'));
  t.after(()=>rm(directory,{recursive:true,force:true}));
  const ssl = (...args)=>execFileSync('openssl',args,{cwd:directory,stdio:'ignore'});
  ssl('req','-new','-x509','-nodes','-newkey','rsa:2048','-keyout','ca.key','-out','ca.crt',
    '-days','1','-subj','/CN=V30 isolated test CA','-addext','basicConstraints=critical,CA:TRUE',
    '-addext','keyUsage=critical,keyCertSign,cRLSign');
  ssl('req','-new','-nodes','-newkey','rsa:2048','-keyout','server.key','-out','server.csr',
    '-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost',
    '-addext','extendedKeyUsage=serverAuth');
  ssl('x509','-req','-in','server.csr','-CA','ca.crt','-CAkey','ca.key','-CAcreateserial',
    '-out','server.crt','-days','1','-copy_extensions','copy');
  const [key,cert,ca] = await Promise.all(['server.key','server.crt','ca.crt']
    .map(file=>readFile(join(directory,file))));
  const issuer = generateKeyPairSync('ed25519');
  const alice = generateKeyPairSync('ed25519'), bob = generateKeyPairSync('ed25519');
  const a = new V30ChannelCredential(channelKey,alice.privateKey);
  const b = new V30ChannelCredential(channelKey.toUpperCase(),bob.privateKey);
  const stateFile = join(directory,'state.json');
  const options = {issuerPublicKey:issuer.publicKey,storage:new V30FileStorage(stateFile),
    grantIssuer:{privateKey:issuer.privateKey,memberships:new Map([[scopeId,a.publicBinding().membershipPublicKey]])}};
  return {options,key,cert,ca,alice,bob,a,b,stateFile};
}

async function start(t,options) {
  const server = await createV30RelayService(options);
  server.listen(0,'127.0.0.1');
  await once(server,'listening');
  const port=server.address().port;
  t.after(async()=>{
    server.closeAllConnections();
    await new Promise((resolve,reject)=>server.close(error=>error?reject(error):resolve()));
  });
  return port;
}

function request(send,port,path,body,tlsOptions={}) {
  return new Promise((resolve,reject)=>{
    const json=JSON.stringify(body);
    const req=send({hostname:'127.0.0.1',port,path,method:'POST',agent:false,
      headers:{'Content-Type':'application/json','Content-Length':Buffer.byteLength(json)},...tlsOptions},res=>{
      const protocol=res.socket.getProtocol?.();
      const chunks=[];
      res.on('data',chunk=>chunks.push(chunk));
      res.on('error',reject);
      res.on('end',()=>{
        const data=Buffer.concat(chunks).toString();
        try {resolve({status:res.statusCode,body:JSON.parse(data),protocol});}
        catch(error){reject(error);}
      });
    });
    req.on('error',reject);
    req.setTimeout(3000,()=>req.destroy(Error('unexpected_timeout')));
    req.end(json);
  });
}

test('independent same-channel clients exchange encrypted messages over verified TLS 1.2',async t=>{
  const f=await fixture(t), port=await start(t,{...f.options,tls:{key:f.key,cert:f.cert}});
  const post=async(path,body)=>{
    const response=await request(httpsRequest,port,path,body,{ca:f.ca,servername:'localhost',
      minVersion:'TLSv1.2',maxVersion:'TLSv1.2'});
    assert.equal(response.protocol,'TLSv1.2');
    assert.equal(response.status,200);
    return response.body;
  };
  const grant=async(credential,right)=>{
    const challenge=await post('/v30/grants/challenge',credential.begin(right));
    return post('/v30/grants/issue',credential.finish(challenge,right));
  };
  assert.notEqual(raw(f.alice.publicKey),raw(f.bob.publicKey));
  const envelope={scopeId,originId:raw(f.alice.publicKey),messageId:'21'.repeat(16)};
  const iv=randomBytes(12);
  const keyA=createHmac('sha256',Buffer.from(channelKey,'hex')).update('MOG30-CH-AES-GCM').digest();
  const cipher=createCipheriv('aes-256-gcm',keyA,iv);
  cipher.setAAD(Buffer.from([scopeId,envelope.originId,envelope.messageId].join('|'),'ascii'));
  const encrypted=Buffer.concat([cipher.update('TLS private hello','utf8'),cipher.final()]);
  envelope.ciphertext=Buffer.concat([iv,cipher.getAuthTag(),encrypted]).toString('base64');
  const pushed=await post('/v30/push',signRequest(f.alice,'/v30/push',{
    envelope,authorizationGrant:await grant(f.a,'push')}));
  assert.equal(pushed.accepted,true);
  const page=await post('/v30/poll',signRequest(f.bob,'/v30/poll',{
    scopeId,consumerOriginId:raw(f.bob.publicKey),authorizationGrant:await grant(f.b,'poll')}));
  assert.equal(page.items.length,1);
  const item=page.items[0], bytes=Buffer.from(item.ciphertext,'base64');
  assert.equal(item.originId,envelope.originId);
  const keyB=createHmac('sha256',Buffer.from(channelKey.toUpperCase(),'hex')).update('MOG30-CH-AES-GCM').digest();
  const decipher=createDecipheriv('aes-256-gcm',keyB,bytes.subarray(0,12));
  decipher.setAAD(Buffer.from([item.scopeId,item.originId,item.messageId].join('|'),'ascii'));
  decipher.setAuthTag(bytes.subarray(12,28));
  assert.equal(Buffer.concat([decipher.update(bytes.subarray(28)),decipher.final()]).toString(),'TLS private hello');
  const persisted=await readFile(f.stateFile,'utf8');
  assert.ok(persisted.includes(envelope.ciphertext));
  assert.ok(!persisted.includes(channelKey));
  assert.ok(!persisted.includes('TLS private hello'));
});

test('TLS endpoint rejects wrong hostname, untrusted certificate, older TLS and plaintext',async t=>{
  const f=await fixture(t), port=await start(t,{...f.options,tls:{key:f.key,cert:f.cert}});
  const body=f.a.begin('poll');
  await assert.rejects(request(httpsRequest,port,'/v30/grants/challenge',body,{servername:'localhost'}),
    error=>['UNABLE_TO_VERIFY_LEAF_SIGNATURE','UNABLE_TO_GET_ISSUER_CERT_LOCALLY','SELF_SIGNED_CERT_IN_CHAIN'].includes(error.code));
  await assert.rejects(request(httpsRequest,port,'/v30/grants/challenge',body,{ca:f.ca,servername:'wrong.example'}),
    {code:'ERR_TLS_CERT_ALTNAME_INVALID'});
  await assert.rejects(request(httpsRequest,port,'/v30/grants/challenge',body,{ca:f.ca,servername:'localhost',
    minVersion:'TLSv1.1',maxVersion:'TLSv1.1',ciphers:'DEFAULT:@SECLEVEL=0'}),
    error=>error.code==='EPROTO' && /alert protocol version/i.test(error.message));
  await assert.rejects(request(httpRequest,port,'/v30/grants/challenge',body),{code:'ECONNRESET'});
  // None of the failed transport attempts reserved the signed challenge.
  const response=await request(httpsRequest,port,'/v30/grants/challenge',body,{ca:f.ca,servername:'localhost'});
  assert.equal(response.status,200);
  assert.ok(['TLSv1.2','TLSv1.3'].includes(response.protocol));
});

test('explicit malformed TLS configuration cannot fall back to an HTTP listener',async t=>{
  const f=await fixture(t);
  for(const tls of [null,{},false,{key:f.key},{cert:f.cert},
    {key:f.key,cert:f.cert,minVersion:'TLSv1.1'}]) {
    await assert.rejects(createV30RelayService({...f.options,tls}),/tls_config_invalid/);
  }
  await assert.rejects(createV30RelayService({...f.options,tls:{key:'bad',cert:'bad'}}));
});
