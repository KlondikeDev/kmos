// This file has been generated in whole or in part by a Large Language Model
// WPA2-PSK 4-way handshake (supplicant side) and CCMP data protection.
// The handshake consumes EAPOL-Key frames and produces EAPOL-Key replies;
// on completion the pairwise transient key is installed for the CCMP
// layer in wifi_aes.c. Tested against the 802.11-2016 J.3 vectors plus a
// live round-trip in buildutils/tests/wifi_handshake.c.
#pragma once
#include <types.h>
#define WPA_PMK_LEN 32
#define WPA_PTK_LEN 64
#define WPA_KCK_LEN 16
#define WPA_KEK_LEN 16
#define WPA_TK_LEN 16
#define WPA_NONCE_LEN 32
#define WPA_REPLAY_LEN 8
#define WPA_MIC_LEN 16
// EAPOL-Key descriptor types and key info bits (802.11-2016 Fig 12-30).
#define WPA_KEYDESC_EAPOL 2
#define WPA_KEYINFO_KEY (1u << 3)
#define WPA_KEYINFO_MIC (1u << 8)
#define WPA_KEYINFO_SECURE (1u << 9)
#define WPA_KEYINFO_ENCRYPTED (1u << 12)
typedef enum {
    WPA_DISCONNECTED = 0,
    WPA_AUTH_ASSOCIATED, // open auth + assoc done, awaiting message 1
    WPA_MSG1_SEEN, // have ANonce, sent message 2
    WPA_MSG3_SEEN, // keys installed, sent message 4
    WPA_COMPLETED, // group handshake done (message 1/2 of group), traffic open
} wpa_state_t;
typedef struct {
    wpa_state_t state;
    u8 pmk[WPA_PMK_LEN];
    u8 ptk[WPA_PTK_LEN];
    u8 anonce[WPA_NONCE_LEN];
    u8 snonce[WPA_NONCE_LEN];
    u8 ap_mac[6];
    u8 sta_mac[6];
    u8 replay[WPA_REPLAY_LEN];
    u8 gtk[WPA_TK_LEN];
    u8 gtk_index;
    bool gtk_ok;
    u64 tx_pn; // CCMP packet number for transmitted frames
    u64 rx_pn; // highest accepted CCMP packet number
} wpa_t;
// Derive the PMK from passphrase + SSID (4096 iterations).
void wpa_derive_pmk(wpa_t* hs, const char* passphrase, const void* ssid, u32 ssid_length);
// Begin after 802.11 association: store addresses, reset state.
void wpa_start(wpa_t* hs, const u8 ap_mac[6], const u8 sta_mac[6]);
// Feed one EAPOL-Key frame (complete EAPOL PDU). When a reply is due,
// build it into reply/reply_capacity and set *reply_length. Returns true
// when the frame belonged to the handshake (even on MIC failure, which
// leaves the state unchanged and requests no reply).
bool wpa_receive(wpa_t* hs, const void* frame, u32 length,
                 void* reply, u32 reply_capacity, u32* reply_length);
// PTK components after completion (KCK, KEK, TK).
void wpa_tk(const wpa_t* hs, u8 out[WPA_TK_LEN]);
bool wpa_completed(const wpa_t* hs);
// AES key unwrap (RFC 3394): remove the 8-byte IV, verify it, return the
// key material. out_capacity must equal wrapped_length - 8.
bool wpa_unwrap(const u8* kek, const u8* wrapped, u32 wrapped_length,
                u8* out, u32 out_capacity);
