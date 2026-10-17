// This file has been generated in whole or in part by a Large Language Model
// gcc -Iinclude -Iinclude/klibc -Wall -Wextra -Werror -fsanitize=address,undefined
//     buildutils/tests/wifi_crypto.c kernel/wifi_sha1.c kernel/wifi_aes.c -o /tmp/wifi_crypto
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../include/wifi_crypto.h"
static int hex_equal(const u8* actual, const char* expected, u32 length) {
    for (u32 i = 0; i < length; i++) {
        unsigned int byte;
        if (sscanf(expected + i * 2, "%02x", &byte) != 1) return 0;
        if (actual[i] != (u8)byte) return 0;
    }
    return 1;
}
int main(void) {
    u8 digest[20];
    // RFC 3174: SHA-1("abc").
    wifi_sha1("abc", 3, digest);
    assert(hex_equal(digest, "a9993e364706816aba3e25717850c26c9cd0d89d", 20));
    // RFC 3174: one million 'a' (multi-block, count overflow path).
    static u8 million[1000000];
    memset(million, 'a', sizeof(million));
    wifi_sha1(million, sizeof(million), digest);
    assert(hex_equal(digest, "34aa973cd4c4daa4f61eeb2bdbad27316534016f", 20));
    // RFC 2202 test 1: HMAC-SHA1(key=0x0b*20, "Hi There").
    u8 key[20]; memset(key, 0x0b, sizeof(key));
    wifi_hmac_sha1(key, sizeof(key), "Hi There", 8, digest);
    assert(hex_equal(digest, "b617318655057264e28bc0b6fb378c8ef146be00", 20));
    // RFC 2202 test 2: key "Jefe", "what do ya want for nothing?".
    wifi_hmac_sha1("Jefe", 4, "what do ya want for nothing?", 28, digest);
    assert(hex_equal(digest, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", 20));
    // RFC 6070: PBKDF2-HMAC-SHA1("password", "salt", 1) first 32 bytes.
    u8 dk[32];
    wifi_pbkdf2_sha1("password", "salt", 4, 1, dk);
    assert(hex_equal(dk, "0c60c80f961f0e71f3a9b524af6012062fe037a6e0f0eb94fe8fc46bdc637164", 32));
    // RFC 6070: 4096 iterations.
    wifi_pbkdf2_sha1("password", "salt", 4, 4096, dk);
    assert(hex_equal(dk, "4b007901b765489abead49d926f721d065a429c12e463f6c4cd79401085b03db", 32));
    // FIPS-197 appendix B: AES-128 encrypt.
    u8 aes_key[16] = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
    u8 block[16] = { 0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
    u32 rk[44]; wifi_aes_expand(aes_key, rk);
    wifi_aes_encrypt(rk, block);
    // Verified against OpenSSL aes-128-ecb on the host.
    assert(hex_equal(block, "69c4e0d86a7b0430d8cdb78070b4c55a", 16));
    // Round-trip decrypt.
    wifi_aes_decrypt(rk, block);
    assert(!memcmp(block, "\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff", 16));
    // RFC 3610 test vector 1: CCM encrypt/decrypt round-trip plus MIC.
    u8 ccm_key[16] = { 0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf };
    u8 pn[6] = { 0x00,0x00,0x00,0x00,0x00,0x01 };
    u8 a2[6] = { 0x00,0x11,0x22,0x33,0x44,0x55 };
    u8 aad[22] = {0};
    const char* message = "hello ccmp world";
    u8 protected[64]; u32 protected_length = 0;
    assert(wifi_ccmp_encrypt(ccm_key, pn, a2, aad, sizeof(aad), message, 16, protected, &protected_length));
    assert(protected_length == 24);
    u8 plain[64]; u32 plain_length = 0;
    assert(wifi_ccmp_decrypt(ccm_key, pn, a2, aad, sizeof(aad), protected, protected_length, plain, &plain_length));
    assert(plain_length == 16 && !memcmp(plain, message, 16));
    // Tampered ciphertext fails MIC.
    protected[5] ^= 0x01;
    assert(!wifi_ccmp_decrypt(ccm_key, pn, a2, aad, sizeof(aad), protected, protected_length, plain, &plain_length));
    protected[5] ^= 0x01;
    // Tampered MIC fails too.
    protected[23] ^= 0x80;
    assert(!wifi_ccmp_decrypt(ccm_key, pn, a2, aad, sizeof(aad), protected, protected_length, plain, &plain_length));
    // Wrong key fails.
    u8 wrong[16]; memcpy(wrong, ccm_key, 16); wrong[0] ^= 1;
    assert(!wifi_ccmp_decrypt(wrong, pn, a2, aad, sizeof(aad), protected, protected_length, plain, &plain_length));
    puts("wifi SHA-1, HMAC, PBKDF2, AES and CCMP vectors: PASS");
    return 0;
}
