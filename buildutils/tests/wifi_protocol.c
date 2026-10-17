// This file has been generated in whole or in part by a Large Language Model
// gcc -Iinclude -Iinclude/klibc -Wall -Wextra -Werror -fsanitize=address,undefined
//     buildutils/tests/wifi_protocol.c kernel/wifi.c -o /tmp/wifi_protocol
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../include/wifi.h"
static const u8 local[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const u8 bssid[6] = { 0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e };
static const u8 other[6] = { 0x00, 0xde, 0xad, 0xbe, 0xef, 0x00 };
// Build a management reply: subtype, body bytes.
static u32 reply(u8* frame, u8 subtype, const u8* body, u32 body_length) {
    frame[0] = (u8)(subtype << 4); frame[1] = 0; frame[2] = frame[3] = 0;
    memcpy(frame + 4, local, 6); memcpy(frame + 10, bssid, 6); memcpy(frame + 16, bssid, 6);
    frame[22] = frame[23] = 0;
    memcpy(frame + 24, body, body_length);
    return 24 + body_length;
}
int main(void) {
    wifi_frame_t parsed;
    u8 junk[10] = {0};
    assert(!wifi_parse(junk, sizeof(junk), &parsed)); // too short
    u8 bad_version[24] = {0}; bad_version[0] = 3;
    assert(!wifi_parse(bad_version, sizeof(bad_version), &parsed)); // version 3
    wifi_sta_t sta;
    wifi_sta_init(&sta, local);
    assert(sta.state == WIFI_IDLE);
    assert(!wifi_sta_join(&sta, bssid, NULL, 0)); // empty SSID
    assert(!wifi_sta_join(&sta, bssid, "x", 33)); // too long
    assert(wifi_sta_join(&sta, bssid, "kunix-test", 10));
    // First transmit: open-system auth, sequence 1.
    u32 length = wifi_next_tx(&sta);
    assert(length == 24 + 6 && sta.state == WIFI_AUTH_SENT);
    assert(sta.last_tx[0] == (WIFI_MGMT_AUTH << 4));
    assert(!memcmp(sta.last_tx + 4, bssid, 6) && !memcmp(sta.last_tx + 10, local, 6));
    assert(sta.last_tx[24] == 0 && sta.last_tx[26] == 1); // open, seq 1
    assert(wifi_next_tx(&sta) == 0); // nothing due while waiting
    // Wrong-source and wrong-destination replies are ignored, not consumed.
    u8 frame[64];
    u8 auth_ok[6] = { 0, 0, 2, 0, 0, 0 };
    u32 n = reply(frame, WIFI_MGMT_AUTH, auth_ok, sizeof(auth_ok));
    memcpy(frame + 10, other, 6);
    assert(!wifi_receive(&sta, frame, n) && sta.state == WIFI_AUTH_SENT);
    memcpy(frame + 10, bssid, 6); memcpy(frame + 4, other, 6);
    assert(!wifi_receive(&sta, frame, n) && sta.state == WIFI_AUTH_SENT);
    memcpy(frame + 4, local, 6);
    // Rejected auth (status nonzero) keeps the state.
    u8 auth_bad[6] = { 0, 0, 2, 0, 1, 0 };
    n = reply(frame, WIFI_MGMT_AUTH, auth_bad, sizeof(auth_bad));
    assert(wifi_receive(&sta, frame, n) && sta.state == WIFI_AUTH_SENT);
    // Accepted auth advances.
    n = reply(frame, WIFI_MGMT_AUTH, auth_ok, sizeof(auth_ok));
    assert(wifi_receive(&sta, frame, n) && sta.state == WIFI_AUTH_OK);
    // Second transmit: association request carrying the SSID.
    length = wifi_next_tx(&sta);
    assert(length > 24 + 4 && sta.state == WIFI_ASSOC_SENT);
    assert(sta.last_tx[0] == (WIFI_MGMT_ASSOC_REQ << 4));
    assert(sta.last_tx[28] == WIFI_IE_SSID && sta.last_tx[29] == 10);
    assert(!memcmp(sta.last_tx + 30, "kunix-test", 10));
    // Association reply with AID 0x0c21 (already below 0x4000, kept whole).
    u8 assoc_ok[6] = { 0, 0, 0, 0, 0x21, 0x0c };
    n = reply(frame, WIFI_MGMT_ASSOC_RESP, assoc_ok, sizeof(assoc_ok));
    assert(wifi_receive(&sta, frame, n) && sta.state == WIFI_ASSOCIATED);
    assert(sta.aid == 0x0c21);
    assert(wifi_next_tx(&sta) == 0); // associated: nothing due
    // Deauthentication drops back to idle.
    n = reply(frame, WIFI_MGMT_DEAUTH, auth_ok, 2);
    assert(wifi_receive(&sta, frame, n) && sta.state == WIFI_IDLE);
    // Data path: re-associate, then convert a from-DS LLC/SNAP frame.
    assert(wifi_sta_join(&sta, bssid, "kunix-test", 10));
    (void)wifi_next_tx(&sta);
    n = reply(frame, WIFI_MGMT_AUTH, auth_ok, sizeof(auth_ok));
    (void)wifi_receive(&sta, frame, n);
    (void)wifi_next_tx(&sta);
    n = reply(frame, WIFI_MGMT_ASSOC_RESP, assoc_ok, sizeof(assoc_ok));
    (void)wifi_receive(&sta, frame, n);
    assert(sta.state == WIFI_ASSOCIATED);
    u8 data[64] = { 0x08, 0x02 }; // type data, from-DS
    memcpy(data + 4, local, 6); memcpy(data + 10, bssid, 6); memcpy(data + 16, other, 6);
    u8 snap[8] = { 0xaa, 0xaa, 0x03, 0, 0, 0, 0x08, 0x00 };
    memcpy(data + 24, snap, 8);
    memcpy(data + 32, "ping", 4);
    u8 ethernet[64];
    u32 ethernet_length = wifi_to_ethernet(&sta, data, 36, ethernet, sizeof(ethernet));
    assert(ethernet_length == 18);
    assert(!memcmp(ethernet, local, 6) && !memcmp(ethernet + 6, other, 6));
    assert(ethernet[12] == 0x08 && ethernet[13] == 0x00);
    assert(!memcmp(ethernet + 14, "ping", 4));
    // Unassociated stations convert nothing.
    wifi_sta_t idle;
    wifi_sta_init(&idle, local);
    assert(wifi_to_ethernet(&idle, data, 36, ethernet, sizeof(ethernet)) == 0);
    // Short body and non-SNAP frames are refused.
    assert(wifi_to_ethernet(&sta, data, 30, ethernet, sizeof(ethernet)) == 0);
    data[24] = 0x00;
    assert(wifi_to_ethernet(&sta, data, 36, ethernet, sizeof(ethernet)) == 0);
    puts("wifi frame layer, open auth/assoc and data conversion: PASS");
    return 0;
}
