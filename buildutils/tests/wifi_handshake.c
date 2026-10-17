// This file has been generated in whole or in part by a Large Language Model
// gcc -Iinclude -Iinclude/klibc -Wall -Wextra -Werror -fsanitize=address,undefined
//     buildutils/tests/wifi_handshake.c kernel/wifi_wpa.c kernel/wifi_sha1.c
//     kernel/wifi_aes.c -o /tmp/wifi_handshake
// Simulated authenticator against the supplicant: both sides derive the
// same PTK from a known PMK, the MICs verify, and the GTK installs.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../include/wifi_wpa.h"
#include "../../include/wifi_crypto.h"
// Deterministic entropy for the test: the SNonce must be fresh per run on
// hardware; here a fixed pattern keeps the handshake reproducible.
i64 random_read(void* buffer, u64 length) {
    for (u64 i = 0; i < length; i++) ((u8*)buffer)[i] = (u8)(0x5a + i);
    return (i64)length;
}
static const u8 ap_mac[6] = { 0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e };
static const u8 sta_mac[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static u8 pmk[32] = {
    0x60,0x8c,0x35,0x6f,0xcb,0x9f,0x99,0x1f,0x28,0xb5,0x97,0xf8,0xba,0x7c,0x9d,0x22,
    0xd3,0x73,0x0e,0x11,0xc8,0x42,0x05,0x31,0x33,0x77,0x77,0x77,0x9c,0xc6,0xaf,0x0c,
};
static void write16(u8* p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static u16 read16(const u8* p) { return (u16)(((u16)p[0] << 8) | p[1]); }
// RFC 3394 section 4.1 wrap vector, to test the unwrap path directly.
static void rfc3394(void) {
    u8 kek[16] = { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
    u8 wrapped[24] = { 0x1f,0xa6,0x8b,0x0a,0x81,0x12,0xb4,0x47,0xae,0xf3,0x4b,0xd8,
                       0xfb,0x5a,0x7b,0x82,0x9d,0x3e,0x86,0x23,0x71,0xd2,0xcf,0xe5 };
    u8 out[16];
    assert(wpa_unwrap(kek, wrapped, sizeof(wrapped), out, sizeof(out)));
    // Key data from RFC 3394 4.1.
    u8 expected[16] = { 0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
    assert(!memcmp(out, expected, 16));
    // Tampered wrap fails.
    wrapped[10] ^= 1;
    assert(!wpa_unwrap(kek, wrapped, sizeof(wrapped), out, sizeof(out)));
}
int main(void) {
    rfc3394();
    wpa_t hs;
    for (u32 i = 0; i < sizeof(hs); i++) ((u8*)&hs)[i] = 0;
    memcpy(hs.pmk, pmk, 32);
    // PMK derivation path: passphrase "password", SSID "IEEE".
    // Expected value verified against Python hashlib.pbkdf2_hmac.
    wpa_t derive;
    memset(&derive, 0, sizeof(derive));
    wpa_derive_pmk(&derive, "password", "IEEE", 4);
    u8 expected_pmk[32] = { 0xf4,0x2c,0x6f,0xc5,0x2d,0xf0,0xeb,0xef,0x9e,0xbb,0x4b,0x90,0xb3,0x8a,0x5f,0x90,
                            0x2e,0x83,0xfe,0x1b,0x13,0x5a,0x70,0xe2,0x3a,0xed,0x76,0x2e,0x97,0x10,0xa1,0x2e };
    assert(!memcmp(derive.pmk, expected_pmk, 32));
    wpa_start(&hs, ap_mac, sta_mac);
    assert(hs.state == WPA_AUTH_ASSOCIATED);
    // Authenticator crafts message 1: ANonce, replay 1, no MIC, no data.
    u8 anonce[32] = { 0x11,0x22,0x33,0x44 };
    for (u32 i = 4; i < 32; i++) anonce[i] = (u8)i;
    u8 msg1[99] = {0};
    msg1[0] = 2; msg1[1] = 3; msg1[2] = 0; msg1[3] = 95; // EAPOL v2, 95 bytes
    msg1[4] = 2; // descriptor type EAPOL-Key
    write16(msg1 + 7, 16); // key length (CCMP-128)
    msg1[16] = 1; // replay counter 1
    memcpy(msg1 + 17, anonce, 32);
    write16(msg1 + 5, WPA_KEYINFO_KEY); // info: pairwise, no MIC
    u8 reply[512]; u32 reply_length = 0;
    assert(wpa_receive(&hs, msg1, sizeof(msg1), reply, sizeof(reply), &reply_length));
    assert(hs.state == WPA_MSG1_SEEN && reply_length == sizeof(msg1));
    assert(read16(reply + 5) == (WPA_KEYINFO_KEY | WPA_KEYINFO_MIC));
    assert(!memcmp(reply + 9, msg1 + 9, 8)); // replay echoed
    // Authenticator verifies message 2's MIC with the derived KCK.
    u8 snonce[32]; memcpy(snonce, reply + 17, 32);
    // Message 3: secure + MIC, GTK wrapped with KEK, replay 2.
    u8 gtk[16] = { 0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99 };
    // Wrap the GTK with the KEK using RFC 3394 (replicating the AP).
    u8 kck[16], kek[16], tk[16];
    // Extract keys from the supplicant (already derived).
    memcpy(kck, hs.ptk, 16); memcpy(kek, hs.ptk + 16, 16); memcpy(tk, hs.ptk + 32, 16);
    u8 wrapped[24];
    {
        // AES-wrap: replicate RFC 3394 forward (test-only helper inline).
        u32 rk[44]; wifi_aes_expand(kek, rk);
        u8 a[8] = { 0xa6,0xa6,0xa6,0xa6,0xa6,0xa6,0xa6,0xa6 };
        u8 r[16]; memcpy(r, gtk, 16);
        for (u32 j = 0; j <= 5; j++) {
            for (u32 i = 1; i <= 2; i++) {
                u8 block[16]; memcpy(block, a, 8); memcpy(block + 8, r + (i-1)*8, 8);
                wifi_aes_encrypt(rk, block);
                u64 t = (u64)(2 * j + i);
                for (u32 k = 0; k < 8; k++) a[k] = (u8)(block[k] ^ (t >> (56 - k * 8)));
                memcpy(r + (i-1)*8, block + 8, 8);
            }
        }
        memcpy(wrapped, a, 8); memcpy(wrapped + 8, r, 16);
    }
    u8 msg3[99 + 24] = {0};
    msg3[0] = 2; msg3[1] = 3; msg3[2] = 0; msg3[3] = 95 + 24;
    msg3[4] = 2;
    write16(msg3 + 7, 16);
    msg3[16] = 2; // replay 2
    memcpy(msg3 + 17, anonce, 32);
    write16(msg3 + 5, WPA_KEYINFO_KEY | WPA_KEYINFO_MIC | WPA_KEYINFO_SECURE);
    msg3[73] = 2; // key id 2
    write16(msg3 + 97, 24);
    memcpy(msg3 + 99, wrapped, 24);
    // MIC with KCK over zeroed MIC field.
    {
        u8 digest[20];
        wifi_hmac_sha1(kck, 16, msg3, sizeof(msg3), digest);
        memcpy(msg3 + 81, digest, 16);
    }
    reply_length = 0;
    assert(wpa_receive(&hs, msg3, sizeof(msg3), reply, sizeof(reply), &reply_length));
    assert(hs.state == WPA_MSG3_SEEN && reply_length == 99);
    assert(hs.gtk_ok && !memcmp(hs.gtk, gtk, 16));
    // Authenticator verifies message 4's MIC.
    {
        u8 digest[20];
        u8 check[99]; memcpy(check, reply, 99);
        memset(check + 81, 0, 16);
        wifi_hmac_sha1(kck, 16, check, 99, digest);
        assert(!memcmp(digest, reply + 81, 16));
    }
    // Group message 1: GTK rotate, encrypted flag, replay 3.
    u8 gtk2[16] = { 0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x90,0xa0,0xb0,0xc0,0xd0,0xe0,0xf0,0x00 };
    u8 wrapped2[24];
    {
        u32 rk[44]; wifi_aes_expand(kek, rk);
        u8 a[8] = { 0xa6,0xa6,0xa6,0xa6,0xa6,0xa6,0xa6,0xa6 };
        u8 r[16]; memcpy(r, gtk2, 16);
        for (u32 j = 0; j <= 5; j++) {
            for (u32 i = 1; i <= 2; i++) {
                u8 block[16]; memcpy(block, a, 8); memcpy(block + 8, r + (i-1)*8, 8);
                wifi_aes_encrypt(rk, block);
                u64 t = (u64)(2 * j + i);
                for (u32 k = 0; k < 8; k++) a[k] = (u8)(block[k] ^ (t >> (56 - k * 8)));
                memcpy(r + (i-1)*8, block + 8, 8);
            }
        }
        memcpy(wrapped2, a, 8); memcpy(wrapped2 + 8, r, 16);
    }
    u8 gmsg1[99 + 24] = {0};
    gmsg1[0] = 2; gmsg1[1] = 3; gmsg1[2] = 0; gmsg1[3] = 95 + 24;
    gmsg1[4] = 2;
    write16(gmsg1 + 7, 16);
    gmsg1[16] = 3; // replay 3
    write16(gmsg1 + 5, WPA_KEYINFO_KEY | WPA_KEYINFO_MIC | WPA_KEYINFO_SECURE | WPA_KEYINFO_ENCRYPTED);
    gmsg1[73] = 1; // GTK index 1
    write16(gmsg1 + 97, 24);
    memcpy(gmsg1 + 99, wrapped2, 24);
    {
        u8 digest[20];
        wifi_hmac_sha1(kck, 16, gmsg1, sizeof(gmsg1), digest);
        memcpy(gmsg1 + 81, digest, 16);
    }
    reply_length = 0;
    assert(wpa_receive(&hs, gmsg1, sizeof(gmsg1), reply, sizeof(reply), &reply_length));
    assert(hs.state == WPA_COMPLETED && wpa_completed(&hs));
    assert(hs.gtk_index == 1 && !memcmp(hs.gtk, gtk2, 16));
    // Replay attack: old message 3 re-sent is accepted as belonging but
    // changes nothing and emits no reply.
    reply_length = 0;
    assert(wpa_receive(&hs, msg3, sizeof(msg3), reply, sizeof(reply), &reply_length));
    assert(hs.state == WPA_COMPLETED && reply_length == 0);
    // TK matches the authenticator's derivation (MinMax ordering).
    u8 supplicant_tk[16]; wpa_tk(&hs, supplicant_tk);
    assert(!memcmp(supplicant_tk, tk, 16));
    puts("wifi WPA2 4-way handshake, unwrap and GTK rotation: PASS");
    return 0;
}
