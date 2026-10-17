// This file has been generated in whole or in part by a Large Language Model
// AES-128 (FIPS-197) and CCM mode (RFC 3610) for CCMP (802.11-2016 12.5.3).
// S-box as a static table; the rest is straight rounds. No timing
// hardening: the threat model here is a coffee-shop sniffer, not a
// local side channel.
#include <wifi_crypto.h>
static const u8 sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};
static const u8 inv_sbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d,
};
static const u8 rcon[10] = { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36 };
static u8 xtime(u8 x) { return (u8)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
void wifi_aes_expand(const u8 key[16], u32 round_keys[44]) {
    for (u32 i = 0; i < 4; i++)
        round_keys[i] = ((u32)key[i*4] << 24) | ((u32)key[i*4+1] << 16) |
                        ((u32)key[i*4+2] << 8) | key[i*4+3];
    for (u32 i = 4; i < 44; i++) {
        u32 t = round_keys[i-1];
        if (i % 4 == 0) {
            t = ((u32)sbox[(t >> 16) & 0xff] << 24) | ((u32)sbox[(t >> 8) & 0xff] << 16) |
                ((u32)sbox[t & 0xff] << 8) | sbox[(t >> 24) & 0xff];
            t ^= (u32)rcon[i/4 - 1] << 24;
        }
        round_keys[i] = round_keys[i-4] ^ t;
    }
}
static void add_round_key(u8 state[16], const u32 rk[4]) {
    for (u32 i = 0; i < 4; i++) {
        state[i*4] ^= (u8)(rk[i] >> 24); state[i*4+1] ^= (u8)(rk[i] >> 16);
        state[i*4+2] ^= (u8)(rk[i] >> 8); state[i*4+3] ^= (u8)rk[i];
    }
}
void wifi_aes_encrypt(const u32 round_keys[44], u8 block[16]) {
    u8 state[16];
    for (u32 i = 0; i < 16; i++) state[i] = block[i];
    add_round_key(state, round_keys);
    for (u32 round = 1; round <= 9; round++) {
        for (u32 i = 0; i < 16; i++) state[i] = sbox[state[i]];
        u8 t[16];
        t[0]=state[0]; t[4]=state[4]; t[8]=state[8]; t[12]=state[12];
        t[1]=state[5]; t[5]=state[9]; t[9]=state[13]; t[13]=state[1];
        t[2]=state[10]; t[6]=state[14]; t[10]=state[2]; t[14]=state[6];
        t[3]=state[15]; t[7]=state[3]; t[11]=state[7]; t[15]=state[11];
        for (u32 c = 0; c < 4; c++) {
            u8 a = t[c*4], b = t[c*4+1], d = t[c*4+2], e = t[c*4+3];
            state[c*4] = (u8)(xtime(a) ^ xtime(b) ^ b ^ d ^ e);
            state[c*4+1] = (u8)(a ^ xtime(b) ^ xtime(d) ^ d ^ e);
            state[c*4+2] = (u8)(a ^ b ^ xtime(d) ^ xtime(e) ^ e);
            state[c*4+3] = (u8)(xtime(a) ^ a ^ b ^ d ^ xtime(e));
        }
        add_round_key(state, round_keys + round * 4);
    }
    for (u32 i = 0; i < 16; i++) state[i] = sbox[state[i]];
    u8 t[16];
    t[0]=state[0]; t[4]=state[4]; t[8]=state[8]; t[12]=state[12];
    t[1]=state[5]; t[5]=state[9]; t[9]=state[13]; t[13]=state[1];
    t[2]=state[10]; t[6]=state[14]; t[10]=state[2]; t[14]=state[6];
    t[3]=state[15]; t[7]=state[3]; t[11]=state[7]; t[15]=state[11];
    // XOR the final round key (big-endian words).
    for (u32 i = 0; i < 4; i++) {
        t[i*4] ^= (u8)(round_keys[40+i] >> 24); t[i*4+1] ^= (u8)(round_keys[40+i] >> 16);
        t[i*4+2] ^= (u8)(round_keys[40+i] >> 8); t[i*4+3] ^= (u8)round_keys[40+i];
    }
    for (u32 i = 0; i < 16; i++) block[i] = t[i];
}
static u8 mul(u8 a, u8 b) {
    u8 p = 0;
    while (b) { if (b & 1) p ^= a; a = xtime(a); b >>= 1; }
    return p;
}
void wifi_aes_decrypt(const u32 round_keys[44], u8 block[16]) {
    u8 state[16];
    for (u32 i = 0; i < 16; i++) state[i] = block[i];
    for (u32 i = 0; i < 4; i++) {
        state[i*4] ^= (u8)(round_keys[40+i] >> 24); state[i*4+1] ^= (u8)(round_keys[40+i] >> 16);
        state[i*4+2] ^= (u8)(round_keys[40+i] >> 8); state[i*4+3] ^= (u8)round_keys[40+i];
    }
    for (u32 round = 9; round >= 1; round--) {
        // Inverse shift rows, then inverse S-box.
        u8 s[16] = {
            state[0], state[13], state[10], state[7],
            state[4], state[1], state[14], state[11],
            state[8], state[5], state[2], state[15],
            state[12], state[9], state[6], state[3],
        };
        for (u32 i = 0; i < 16; i++) s[i] = inv_sbox[s[i]];
        for (u32 i = 0; i < 4; i++) {
            s[i*4] ^= (u8)(round_keys[round*4+i] >> 24);
            s[i*4+1] ^= (u8)(round_keys[round*4+i] >> 16);
            s[i*4+2] ^= (u8)(round_keys[round*4+i] >> 8);
            s[i*4+3] ^= (u8)round_keys[round*4+i];
        }
        // Inverse mix columns every pass through the loop (FIPS-197 Fig 12).
        {
            u8 t[16];
            for (u32 c = 0; c < 4; c++) {
                u8 a = s[c*4], b = s[c*4+1], d = s[c*4+2], e = s[c*4+3];
                t[c*4] = (u8)(mul(a,0x0e) ^ mul(b,0x0b) ^ mul(d,0x0d) ^ mul(e,0x09));
                t[c*4+1] = (u8)(mul(a,0x09) ^ mul(b,0x0e) ^ mul(d,0x0b) ^ mul(e,0x0d));
                t[c*4+2] = (u8)(mul(a,0x0d) ^ mul(b,0x09) ^ mul(d,0x0e) ^ mul(e,0x0b));
                t[c*4+3] = (u8)(mul(a,0x0b) ^ mul(b,0x0d) ^ mul(d,0x09) ^ mul(e,0x0e));
            }
            for (u32 i = 0; i < 16; i++) s[i] = t[i];
        }
        for (u32 i = 0; i < 16; i++) state[i] = s[i];
    }
    // Final round: inverse shift rows, inverse S-box, first round key.
    {
        u8 s[16] = {
            state[0], state[13], state[10], state[7],
            state[4], state[1], state[14], state[11],
            state[8], state[5], state[2], state[15],
            state[12], state[9], state[6], state[3],
        };
        for (u32 i = 0; i < 16; i++) state[i] = inv_sbox[s[i]];
    }
    for (u32 i = 0; i < 4; i++) {
        state[i*4] ^= (u8)(round_keys[i] >> 24); state[i*4+1] ^= (u8)(round_keys[i] >> 16);
        state[i*4+2] ^= (u8)(round_keys[i] >> 8); state[i*4+3] ^= (u8)round_keys[i];
    }
    for (u32 i = 0; i < 16; i++) block[i] = state[i];
}
// --- CCM (RFC 3610) with M=8, L=2 for CCMP ---
static void ccm_nonce(u8 nonce[13], const u8 pn[6], const u8 a2[6]) {
    nonce[0] = 0; // priority field: data frames use priority 0
    for (u32 i = 0; i < 6; i++) nonce[1 + i] = a2[i];
    for (u32 i = 0; i < 6; i++) nonce[7 + i] = pn[5 - i];
}
static void ctr_block(u8 out[16], const u8 nonce[13], u32 counter) {
    out[0] = 1; // flags: L-1 = 1
    for (u32 i = 0; i < 13; i++) out[1 + i] = nonce[i];
    out[14] = (u8)(counter >> 8); out[15] = (u8)counter;
}
bool wifi_ccmp_encrypt(const u8 key[16], const u8 pn[6], const u8 a2[6],
                       const void* aad, u32 aad_length,
                       const void* plaintext, u32 plain_length,
                       void* output, u32* length) {
    if (!key || !pn || !a2 || !output || !length) return false;
    if (aad_length != 22 && aad_length != 28) return false;
    if (plain_length > 2296) return false;
    u32 rk[44]; wifi_aes_expand(key, rk);
    u8 nonce[13]; ccm_nonce(nonce, pn, a2);
    // B0: flags(0x59) || nonce || length
    u8 b0[16] = { 0x59 };
    for (u32 i = 0; i < 13; i++) b0[1 + i] = nonce[i];
    b0[14] = (u8)(plain_length >> 8); b0[15] = (u8)plain_length;
    u8 x[16];
    for (u32 i = 0; i < 16; i++) x[i] = b0[i];
    wifi_aes_encrypt(rk, x);
    // AAD length block: 0x00 0x00 || aad_length BE, then AAD padded.
    u8 alen[16] = { 0, 0, (u8)(aad_length >> 8), (u8)aad_length };
    for (u32 i = 0; i < 16; i++) x[i] ^= alen[i];
    wifi_aes_encrypt(rk, x);
    const u8* ad = aad;
    u32 remaining = aad_length;
    while (remaining) {
        u8 block[16] = {0};
        u32 take = remaining < 16 ? remaining : 16;
        for (u32 i = 0; i < take; i++) block[i] = ad[i];
        for (u32 i = 0; i < 16; i++) x[i] ^= block[i];
        wifi_aes_encrypt(rk, x);
        ad += take; remaining -= take;
    }
    // Payload blocks into CBC-MAC.
    const u8* pt = plaintext;
    remaining = plain_length;
    while (remaining) {
        u8 block[16] = {0};
        u32 take = remaining < 16 ? remaining : 16;
        for (u32 i = 0; i < take; i++) block[i] = pt[i];
        for (u32 i = 0; i < 16; i++) x[i] ^= block[i];
        wifi_aes_encrypt(rk, x);
        pt += take; remaining -= take;
    }
    // S0 masks the MIC; S1.. mask the payload.
    u8 s0[16]; ctr_block(s0, nonce, 0); wifi_aes_encrypt(rk, s0);
    u8* out = output;
    u32 counter = 1;
    pt = plaintext; remaining = plain_length;
    u8 stream[16]; u32 stream_used = 16;
    while (remaining) {
        if (stream_used == 16) {
            u8 ctr[16]; ctr_block(ctr, nonce, counter++);
            wifi_aes_encrypt(rk, ctr);
            for (u32 i = 0; i < 16; i++) stream[i] = ctr[i];
            stream_used = 0;
        }
        *out++ = *pt++ ^ stream[stream_used++];
        remaining--;
    }
    for (u32 i = 0; i < 8; i++) *out++ = (u8)(x[i] ^ s0[i]);
    *length = plain_length + 8;
    return true;
}
bool wifi_ccmp_decrypt(const u8 key[16], const u8 pn[6], const u8 a2[6],
                       const void* aad, u32 aad_length,
                       const void* protected, u32 protected_length,
                       void* output, u32* length) {
    if (!key || !pn || !a2 || !output || !length) return false;
    if (aad_length != 22 && aad_length != 28) return false;
    if (protected_length < 8 || protected_length - 8 > 2296) return false;
    u32 plain_length = protected_length - 8;
    u32 rk[44]; wifi_aes_expand(key, rk);
    u8 nonce[13]; ccm_nonce(nonce, pn, a2);
    // Decrypt first (CTR), then verify the MIC (CBC-MAC).
    const u8* ct = protected;
    u8* pt = output;
    u32 counter = 1;
    u8 stream[16]; u32 stream_used = 16;
    for (u32 i = 0; i < plain_length; i++) {
        if (stream_used == 16) {
            u8 ctr[16]; ctr_block(ctr, nonce, counter++);
            wifi_aes_encrypt(rk, ctr);
            for (u32 j = 0; j < 16; j++) stream[j] = ctr[j];
            stream_used = 0;
        }
        pt[i] = ct[i] ^ stream[stream_used++];
    }
    // Recompute the MIC over the decrypted payload and compare.
    u8 b0[16] = { 0x59 };
    for (u32 i = 0; i < 13; i++) b0[1 + i] = nonce[i];
    b0[14] = (u8)(plain_length >> 8); b0[15] = (u8)plain_length;
    u8 x[16];
    for (u32 i = 0; i < 16; i++) x[i] = b0[i];
    wifi_aes_encrypt(rk, x);
    u8 alen[16] = { 0, 0, (u8)(aad_length >> 8), (u8)aad_length };
    for (u32 i = 0; i < 16; i++) x[i] ^= alen[i];
    wifi_aes_encrypt(rk, x);
    const u8* ad = aad;
    u32 remaining = aad_length;
    while (remaining) {
        u8 block[16] = {0};
        u32 take = remaining < 16 ? remaining : 16;
        for (u32 i = 0; i < take; i++) block[i] = ad[i];
        for (u32 i = 0; i < 16; i++) x[i] ^= block[i];
        wifi_aes_encrypt(rk, x);
        ad += take; remaining -= take;
    }
    remaining = plain_length;
    u32 offset = 0;
    while (remaining) {
        u8 block[16] = {0};
        u32 take = remaining < 16 ? remaining : 16;
        for (u32 i = 0; i < take; i++) block[i] = pt[offset + i];
        for (u32 i = 0; i < 16; i++) x[i] ^= block[i];
        wifi_aes_encrypt(rk, x);
        offset += take; remaining -= take;
    }
    u8 s0[16]; ctr_block(s0, nonce, 0); wifi_aes_encrypt(rk, s0);
    u8 diff = 0;
    for (u32 i = 0; i < 8; i++) diff |= (u8)((x[i] ^ s0[i]) ^ ct[plain_length + i]);
    if (diff) return false;
    *length = plain_length;
    return true;
}
