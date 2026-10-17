// This file has been generated in whole or in part by a Large Language Model
// SHA-1 (FIPS 180-4), HMAC-SHA-1 (RFC 2104) and PBKDF2-SHA1 (RFC 2898).
// Straightforward implementation: one context on the caller's stack,
// explicit big-endian loads, no table beyond the round constants.
#include <wifi_crypto.h>
typedef struct { u32 state[5]; u64 count; u8 buffer[64]; u32 used; } sha1_t;
static u32 rol(u32 value, u32 bits) { return (value << bits) | (value >> (32 - bits)); }
static void sha1_block(sha1_t* c, const u8 block[64]) {
    u32 w[80];
    for (u32 i = 0; i < 16; i++)
        w[i] = ((u32)block[i*4] << 24) | ((u32)block[i*4+1] << 16) |
               ((u32)block[i*4+2] << 8) | block[i*4+3];
    for (u32 i = 16; i < 80; i++)
        w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    u32 a = c->state[0], b = c->state[1], cc = c->state[2], dd = c->state[3], ee = c->state[4];
    for (u32 i = 0; i < 80; i++) {
        u32 g, k;
        if (i < 20) { g = (b & cc) | (~b & dd); k = 0x5a827999u; }
        else if (i < 40) { g = b ^ cc ^ dd; k = 0x6ed9eba1u; }
        else if (i < 60) { g = (b & cc) | (b & dd) | (cc & dd); k = 0x8f1bbcdcu; }
        else { g = b ^ cc ^ dd; k = 0xca62c1d6u; }
        u32 t = rol(a, 5) + g + ee + k + w[i];
        ee = dd; dd = cc; cc = rol(b, 30); b = a; a = t;
    }
    c->state[0] += a; c->state[1] += b; c->state[2] += cc;
    c->state[3] += dd; c->state[4] += ee;
}
static void sha1_init(sha1_t* c) {
    c->state[0] = 0x67452301u; c->state[1] = 0xefcdab89u; c->state[2] = 0x98badcfeu;
    c->state[3] = 0x10325476u; c->state[4] = 0xc3d2e1f0u;
    c->count = 0; c->used = 0;
}
static void sha1_update(sha1_t* c, const void* data, u32 length) {
    const u8* p = data;
    c->count += length;
    while (length) {
        u32 room = 64 - c->used;
        u32 take = length < room ? length : room;
        for (u32 i = 0; i < take; i++) c->buffer[c->used + i] = p[i];
        c->used += take; p += take; length -= take;
        if (c->used == 64) { sha1_block(c, c->buffer); c->used = 0; }
    }
}
static void sha1_final(sha1_t* c, u8 out[20]) {
    u64 bits = c->count * 8;
    u8 padding = 0x80;
    sha1_update(c, &padding, 1);
    u8 zero = 0;
    while (c->used != 56) sha1_update(c, &zero, 1);
    u8 length_bytes[8];
    for (u32 i = 0; i < 8; i++) length_bytes[i] = (u8)(bits >> (56 - i * 8));
    // Append the length without re-entering update's count.
    for (u32 i = 0; i < 8; i++) c->buffer[c->used + i] = length_bytes[i];
    sha1_block(c, c->buffer);
    for (u32 i = 0; i < 5; i++) {
        out[i*4] = (u8)(c->state[i] >> 24); out[i*4+1] = (u8)(c->state[i] >> 16);
        out[i*4+2] = (u8)(c->state[i] >> 8); out[i*4+3] = (u8)c->state[i];
    }
}
void wifi_sha1(const void* message, u32 length, u8 out[20]) {
    sha1_t c; sha1_init(&c);
    if (length) sha1_update(&c, message, length);
    sha1_final(&c, out);
}
void wifi_hmac_sha1(const void* key, u32 key_length,
                    const void* message, u32 message_length, u8 out[20]) {
    u8 condensed[20];
    const u8* k = key;
    if (key_length > 64) { wifi_sha1(key, key_length, condensed); k = condensed; key_length = 20; }
    u8 padded[64];
    for (u32 i = 0; i < 64; i++) padded[i] = i < key_length ? k[i] : 0;
    u8 inner[64], outer[64];
    for (u32 i = 0; i < 64; i++) { inner[i] = padded[i] ^ 0x36; outer[i] = padded[i] ^ 0x5c; }
    sha1_t c; sha1_init(&c); sha1_update(&c, inner, 64);
    if (message_length) sha1_update(&c, message, message_length);
    u8 digest[20]; sha1_final(&c, digest);
    sha1_init(&c); sha1_update(&c, outer, 64); sha1_update(&c, digest, 20);
    sha1_final(&c, out);
}
void wifi_pbkdf2_sha1(const char* passphrase,
                      const void* ssid, u32 ssid_length, u32 iterations,
                      u8 out[32]) {
    u32 pass_length = 0;
    while (passphrase[pass_length]) pass_length++;
    // U_1 = PRF(pass, ssid || INT(1)), U_n chained; F = xor of iterations.
    for (u32 block = 1; block <= 2; block++) {
        u8 salted[128];
        for (u32 i = 0; i < ssid_length && i < sizeof(salted) - 4; i++)
            salted[i] = ((const u8*)ssid)[i];
        salted[ssid_length] = 0; salted[ssid_length+1] = 0;
        salted[ssid_length+2] = 0; salted[ssid_length+3] = (u8)block;
        u8 u[20], chained[20];
        wifi_hmac_sha1(passphrase, pass_length, salted, ssid_length + 4, u);
        for (u32 i = 0; i < 20; i++) chained[i] = u[i];
        for (u32 i = 1; i < iterations; i++) {
            wifi_hmac_sha1(passphrase, pass_length, u, 20, u);
            for (u32 j = 0; j < 20; j++) chained[j] ^= u[j];
        }
        u32 copy = block == 1 ? 20 : 12;
        for (u32 i = 0; i < copy; i++) out[(block-1)*20 + i] = chained[i];
    }
}
