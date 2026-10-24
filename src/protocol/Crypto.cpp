#include "Crypto.h"
#include "mbedtls/aes.h"
#include <cstring>

namespace edk {

// Keys recovered from the official app (see README, "Bluetooth protocol").
static const uint8_t kAesKey[16] = {0x80, 0x29, 0x47, 0x46, 0xdb, 0x35,
                                    0x4d, 0xb7, 0x4c, 0x37, 0x01, 0xcf,
                                    0x30, 0xef, 0xdd, 0x65};
static const uint8_t kAesIv[16] = {0xda, 0x1a, 0x55, 0x73, 0x49, 0xf2,
                                   0x5c, 0x64, 0x1b, 0x1a, 0x21, 0xd2,
                                   0x6f, 0x5b, 0x21, 0x8a};

void aesEncryptBlock(const uint8_t in[32], uint8_t out[32]) {
  uint8_t iv[16];
  memcpy(iv, kAesIv, sizeof(iv));
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, kAesKey, 128);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, 32, iv, in, out);
  mbedtls_aes_free(&aes);
}

void aesDecryptBlock(const uint8_t in[32], uint8_t out[32]) {
  uint8_t iv[16];
  memcpy(iv, kAesIv, sizeof(iv));
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, kAesKey, 128);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, 32, iv, in, out);
  mbedtls_aes_free(&aes);
}

} // namespace edk
