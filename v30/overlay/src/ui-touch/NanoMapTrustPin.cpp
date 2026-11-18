#include "NanoMapTrustPin.h"
#include "NanoMapImage.h"
#if defined(ESP32)
#include <mbedtls/sha256.h>
#elif defined(MESH_OFFGRIDNL_NANOMAP_HOST_TEST)
#include <openssl/evp.h>
#endif
namespace meshtastic::nanomap {
namespace {
int digit(char ch) {
  if(ch>='0'&&ch<='9')return ch-'0';
  if(ch>='a'&&ch<='f')return ch-'a'+10;
  return -1;
}
}
bool validTrustPin(const char* hex) {
  if(!hex)return false;
  for(unsigned i=0;i<64;++i)if(digit(hex[i])<0)return false;
  return hex[64]=='\0';
}
bool verifyTrustPin(const uint8_t* bytes,size_t size,const char* hex,MapProgress progress) {
  if(!bytes||!size||size>kRawPackLimit||!validTrustPin(hex)||!progress.proceed())return false;
  uint8_t digest[32]{};
#if defined(ESP32)
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  bool ok=mbedtls_sha256_starts_ret(&context,0)==0;
  const auto update=[&](const uint8_t* p,size_t n){return mbedtls_sha256_update_ret(&context,p,n)==0;};
#elif defined(MESH_OFFGRIDNL_NANOMAP_HOST_TEST)
  EVP_MD_CTX* context=EVP_MD_CTX_new();
  if(!context)return false;
  bool ok=EVP_DigestInit_ex(context,EVP_sha256(),nullptr)==1;
  const auto update=[&](const uint8_t* p,size_t n){return EVP_DigestUpdate(context,p,n)==1;};
#else
  return false;
#endif
#if defined(ESP32)||defined(MESH_OFFGRIDNL_NANOMAP_HOST_TEST)
  for(size_t offset=0;ok&&offset<size;) {
    const size_t n=(size-offset)>16384?16384:size-offset;
    ok=progress.proceed()&&update(bytes+offset,n);offset+=n;
  }
#if defined(ESP32)
  ok=ok&&mbedtls_sha256_finish_ret(&context,digest)==0;
  mbedtls_sha256_free(&context);
#else
  unsigned length=0;
  ok=ok&&EVP_DigestFinal_ex(context,digest,&length)==1&&length==sizeof digest;
  EVP_MD_CTX_free(context);
#endif
  if(!ok||!progress.proceed())return false;
  unsigned difference=0;
  for(unsigned i=0;i<32;++i)difference|=digest[i]^((digit(hex[2*i])<<4)|digit(hex[2*i+1]));
  return difference==0;
#endif
}
}
