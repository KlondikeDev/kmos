// This file has been generated in whole or in part by a Large Language Model
// WPA2 software crypto: SHA-1, HMAC-SHA-1, PBKDF2-SHA1, AES-128 and
// CCM/CCMP. No allocation, no secrets left on the stack beyond the call.
// Tested against RFC 3174, RFC 2202, RFC 6070, FIPS-197 and RFC 3610
// vectors in buildutils/tests/wifi_crypto.c.
#pragma once
#include <types.h>
// SHA-1: digest a message in one call.
void wifi_sha1(const void* message, u32 length, u8 out[20]);
// HMAC-SHA-1 with an arbitrary key.
void wifi_hmac_sha1(const void* key, u32 key_length,
                    const void* message, u32 message_length, u8 out[20]);
// PBKDF2-HMAC-SHA1: 256-bit output for WPA2-PSK passphrases.
void wifi_pbkdf2_sha1(const char* passphrase,
                      const void* ssid, u32 ssid_length, u32 iterations,
                      u8 out[32]);
// AES-128: expand a 16-byte key, then encrypt/decrypt one block in place.
void wifi_aes_expand(const u8 key[16], u32 round_keys[44]);
void wifi_aes_encrypt(const u32 round_keys[44], u8 block[16]);
void wifi_aes_decrypt(const u32 round_keys[44], u8 block[16]);
// CCMP: protect (encrypt + MIC) or unprotect one MPDU payload. The nonce
// follows 802.11-2016 12.5.3.3.3 from the packet number, A2 and priority.
// aad_length is 22 or 28 (with QoS). Returns false on MIC failure (decrypt)
// or bad lengths. The MIC (8 bytes) is appended on encrypt, verified and
// stripped on decrypt; *length updated in place.
bool wifi_ccmp_encrypt(const u8 key[16], const u8 pn[6], const u8 a2[6],
                       const void* aad, u32 aad_length,
                       const void* plaintext, u32 plain_length,
                       void* output, u32* length);
bool wifi_ccmp_decrypt(const u8 key[16], const u8 pn[6], const u8 a2[6],
                       const void* aad, u32 aad_length,
                       const void* protected, u32 protected_length,
                       void* output, u32* length);
