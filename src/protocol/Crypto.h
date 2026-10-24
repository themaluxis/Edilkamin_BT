// AES-128-CBC wrapper for the Edilkamin BLE payload (platform layer, mbedtls).
#pragma once
#include <cstdint>

namespace edk {

void aesEncryptBlock(const uint8_t in[32], uint8_t out[32]);
void aesDecryptBlock(const uint8_t in[32], uint8_t out[32]);

} // namespace edk
