// This file has been generated in whole or in part by a Large Language Model
// WPA2-PSK 4-way handshake, supplicant side (802.11-2016 12.7.6).
// PTK = PRF-X(PMK, "Pairwise key expansion", MinMax(MACs) || MinMax(Nonces)).
// KCK verifies message 1/3 MICs (HMAC-SHA1-128); message 2/4 carry the MIC.
// The group message 1 installs the GTK (encrypted with KEK when present,
// plain otherwise); its message 2 acknowledges. Replay counters must
// strictly increase; the SNonce is hardware-random per association.
#include <wifi_wpa.h>
#include <wifi_crypto.h>
#include <random.h>
static void copy_bytes(u8* out, const u8* in, u32 length) {
    for (u32 i = 0; i < length; i++) out[i] = in[i];
}
static bool bytes_equal(const u8* a, const u8* b, u32 length) {
    u8 diff = 0; for (u32 i = 0; i < length; i++) diff |= a[i] ^ b[i];
    return !diff;
}
static void zero_bytes(u8* p, u32 length) { for (u32 i = 0; i < length; i++) p[i] = 0; }
// PRF-X per 802.11-2016 12.7.1.3: HMAC-SHA1 chained over label || 0x00 ||
// data || counter, first X bytes kept.
static void prf_x(const u8* key, u32 key_length, const char* label,
                  const u8* data, u32 data_length, u8* out, u32 out_length) {
    u32 label_length = 0;
    while (label[label_length]) label_length++;
    u8 message[128];
    u32 produced = 0;
    u8 counter = 0;
    while (produced < out_length) {
        u32 offset = 0;
        for (u32 i = 0; i < label_length && offset < sizeof(message); i++) message[offset++] = (u8)label[i];
        if (offset < sizeof(message)) message[offset++] = 0;
        for (u32 i = 0; i < data_length && offset < sizeof(message); i++) message[offset++] = data[i];
        if (offset < sizeof(message)) message[offset++] = counter;
        u8 digest[20];
        wifi_hmac_sha1(key, key_length, message, offset, digest);
        u32 take = out_length - produced < 20 ? out_length - produced : 20;
        for (u32 i = 0; i < take; i++) out[produced + i] = digest[i];
        produced += take;
        counter++;
    }
}
void wpa_derive_pmk(wpa_t* hs, const char* passphrase, const void* ssid, u32 ssid_length) {
    if (!hs || !passphrase || (!ssid && ssid_length)) return;
    if (ssid_length > 32) return;
    wifi_pbkdf2_sha1(passphrase, ssid, ssid_length, 4096, hs->pmk);
}
void wpa_start(wpa_t* hs, const u8 ap_mac[6], const u8 sta_mac[6]) {
    if (!hs) return;
    for (u32 i = 0; i < sizeof(*hs); i++) ((u8*)hs)[i] = 0;
    if (ap_mac) copy_bytes(hs->ap_mac, ap_mac, 6);
    if (sta_mac) copy_bytes(hs->sta_mac, sta_mac, 6);
    hs->state = WPA_AUTH_ASSOCIATED;
}
// EAPOL-Key layout offsets from the PDU start (802.11-2016 Fig 12-30):
// EAPOL header (version, type, length) then the descriptor.
#define EAPOL_TYPE 1
#define KEY_DESC_TYPE 4
#define KEY_INFO 5
#define KEY_LENGTH 7
#define KEY_REPLAY 9
#define KEY_NONCE 17
#define KEY_IV 49
#define KEY_RSC 65
#define KEY_ID 73
#define KEY_MIC 81
#define KEY_DATA_LENGTH 97
#define KEY_DATA 99
static u16 read16(const u8* p) { return (u16)(((u16)p[0] << 8) | p[1]); }
static void write16(u8* p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
// Derive the PTK once both nonces are known.
static void derive_ptk(wpa_t* hs) {
    u8 data[76]; // MinMax(MACs, 12) || MinMax(Nonces, 64)
    // Order the MACs and nonces numerically (Min || Max).
    const u8* first_mac = hs->ap_mac;
    const u8* second_mac = hs->sta_mac;
    for (u32 i = 0; i < 6; i++) {
        if (hs->ap_mac[i] != hs->sta_mac[i]) {
            if (hs->ap_mac[i] > hs->sta_mac[i]) { first_mac = hs->sta_mac; second_mac = hs->ap_mac; }
            break;
        }
    }
    copy_bytes(data, first_mac, 6); copy_bytes(data + 6, second_mac, 6);
    const u8* first_nonce = hs->anonce;
    const u8* second_nonce = hs->snonce;
    for (u32 i = 0; i < WPA_NONCE_LEN; i++) {
        if (hs->anonce[i] != hs->snonce[i]) {
            if (hs->anonce[i] > hs->snonce[i]) { first_nonce = hs->snonce; second_nonce = hs->anonce; }
            break;
        }
    }
    copy_bytes(data + 12, first_nonce, 32); copy_bytes(data + 44, second_nonce, 32);
    prf_x(hs->pmk, WPA_PMK_LEN, "Pairwise key expansion", data, sizeof(data), hs->ptk, WPA_PTK_LEN);
}
static bool replay_newer(const u8* a, const u8* b) {
    // True when a > b as a big-endian counter.
    for (u32 i = 0; i < WPA_REPLAY_LEN; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}
// Verify the MIC over the frame with the MIC field zeroed.
static bool verify_mic(wpa_t* hs, const u8* frame, u32 length) {
    if (length < KEY_DATA) return false;
    u8 scratch[256];
    if (length > sizeof(scratch)) return false;
    copy_bytes(scratch, frame, length);
    zero_bytes(scratch + KEY_MIC, WPA_MIC_LEN);
    u8 digest[20];
    wifi_hmac_sha1(hs->ptk, WPA_KCK_LEN, scratch, length, digest);
    return bytes_equal(digest, frame + KEY_MIC, WPA_MIC_LEN);
}
static void fill_mic(wpa_t* hs, u8* frame, u32 length) {
    zero_bytes(frame + KEY_MIC, WPA_MIC_LEN);
    u8 digest[20];
    wifi_hmac_sha1(hs->ptk, WPA_KCK_LEN, frame, length, digest);
    copy_bytes(frame + KEY_MIC, digest, WPA_MIC_LEN);
}
bool wpa_receive(wpa_t* hs, const void* frame, u32 length,
                 void* reply, u32 reply_capacity, u32* reply_length) {
    if (reply_length) *reply_length = 0;
    if (!hs || !frame || length < KEY_DATA || length > 512) return false;
    const u8* key = frame;
    if (key[0] != 2 || key[1] != 3 || key[EAPOL_TYPE] != 3 || key[KEY_DESC_TYPE] != WPA_KEYDESC_EAPOL)
        return false;
    u16 info = read16(key + KEY_INFO);
    u16 key_length = read16(key + KEY_LENGTH);
    u16 data_length = read16(key + KEY_DATA_LENGTH);
    if ((u32)KEY_DATA + data_length != length) return false;
    bool mic = (info & WPA_KEYINFO_MIC) != 0;
    bool secure = (info & WPA_KEYINFO_SECURE) != 0;
    bool encrypted = (info & WPA_KEYINFO_ENCRYPTED) != 0;
    // Message 1 of 4: no MIC, contains ANonce.
    if (hs->state == WPA_AUTH_ASSOCIATED && !mic && !secure && !encrypted && key_length == 16) {
        copy_bytes(hs->anonce, key + KEY_NONCE, WPA_NONCE_LEN);
        copy_bytes(hs->replay, key + KEY_REPLAY, WPA_REPLAY_LEN);
        // Fresh SNonce from hardware entropy; bounded retries, explicit
        // failure when the CPU has no RNG (no weak fallback for keys).
        for (u32 attempt = 0; attempt < 3; attempt++) {
            if (random_read(hs->snonce, WPA_NONCE_LEN) == WPA_NONCE_LEN) break;
            if (attempt == 2) return true;
        }
        derive_ptk(hs);
        // Message 2: SNonce, MIC set, replay echoed, RSN IE in key data.
        if (!reply || reply_capacity < length) return true;
        u8* out = reply;
        copy_bytes(out, key, length);
        write16(out + KEY_INFO, WPA_KEYINFO_KEY | WPA_KEYINFO_MIC);
        copy_bytes(out + KEY_REPLAY, hs->replay, WPA_REPLAY_LEN);
        copy_bytes(out + KEY_NONCE, hs->snonce, WPA_NONCE_LEN);
        fill_mic(hs, out, length);
        if (reply_length) *reply_length = length;
        hs->state = WPA_MSG1_SEEN;
        return true;
    }
    // Messages with a MIC require a derived PTK.
    if (hs->state != WPA_MSG1_SEEN && hs->state != WPA_MSG3_SEEN) return hs->state == WPA_COMPLETED;
    if (!mic) return true;
    if (!replay_newer(key + KEY_REPLAY, hs->replay) && hs->state == WPA_MSG3_SEEN) return true;
    if (!verify_mic(hs, key, length)) return true;
    copy_bytes(hs->replay, key + KEY_REPLAY, WPA_REPLAY_LEN);
    // Message 3 of 4: secure, MIC, carries the KEK-wrapped GTK in key
    // data (RFC 3394). Unwrap only; a failed IV check leaves gtk_ok clear
    // but the handshake continues (some APs rotate the GTK right away).
    if (hs->state == WPA_MSG1_SEEN && secure && key_length == 16) {
        if (data_length >= 16 && data_length <= 40 && !(data_length % 8)) {
            u8 unwrapped[32];
            if (wpa_unwrap(hs->ptk + WPA_KCK_LEN, key + KEY_DATA, data_length,
                           unwrapped, data_length - 8)) {
                copy_bytes(hs->gtk, unwrapped, data_length - 8 > 16 ? 16 : data_length - 8);
                hs->gtk_ok = true;
            }
        }
        // Message 4: empty key data, MIC, secure.
        if (!reply || reply_capacity < length) return true;
        u8* out = reply;
        copy_bytes(out, key, length);
        // Zero the key data for message 4.
        for (u32 i = 0; i < data_length && KEY_DATA + i < reply_capacity; i++) out[KEY_DATA + i] = 0;
        *(out + KEY_DATA_LENGTH) = 0; *(out + KEY_DATA_LENGTH + 1) = 0;
        u32 reply_len = KEY_DATA;
        write16(out + KEY_INFO, WPA_KEYINFO_KEY | WPA_KEYINFO_MIC | WPA_KEYINFO_SECURE);
        copy_bytes(out + KEY_REPLAY, hs->replay, WPA_REPLAY_LEN);
        zero_bytes(out + KEY_NONCE, WPA_NONCE_LEN);
        fill_mic(hs, out, reply_len);
        if (reply_length) *reply_length = reply_len;
        hs->state = WPA_MSG3_SEEN;
        return true;
    }
    // Group message 1 of 2: secure, MIC, encrypted; install GTK, ack.
    if (hs->state == WPA_MSG3_SEEN && secure && encrypted) {
        if (data_length >= 16 && data_length <= 40) {
            u8 unwrapped[40];
            if (wpa_unwrap(hs->ptk + WPA_KCK_LEN, key + KEY_DATA, data_length, unwrapped, data_length - 8)) {
                copy_bytes(hs->gtk, unwrapped, 16);
                hs->gtk_index = key[KEY_ID];
                hs->gtk_ok = true;
            }
        }
        if (!reply || reply_capacity < length) return true;
        u8* out = reply;
        copy_bytes(out, key, length);
        for (u32 i = 0; i < data_length && KEY_DATA + i < reply_capacity; i++) out[KEY_DATA + i] = 0;
        *(out + KEY_DATA_LENGTH) = 0; *(out + KEY_DATA_LENGTH + 1) = 0;
        u32 reply_len = KEY_DATA;
        write16(out + KEY_INFO, WPA_KEYINFO_KEY | WPA_KEYINFO_MIC | WPA_KEYINFO_SECURE);
        copy_bytes(out + KEY_REPLAY, hs->replay, WPA_REPLAY_LEN);
        zero_bytes(out + KEY_NONCE, WPA_NONCE_LEN);
        fill_mic(hs, out, reply_len);
        if (reply_length) *reply_length = reply_len;
        hs->state = WPA_COMPLETED;
        return true;
    }
    return true;
}
// AES key unwrap (RFC 3394) with the KEK: 8-byte IV check, n 64-bit blocks.
bool wpa_unwrap(const u8* kek, const u8* wrapped, u32 wrapped_length,
                u8* out, u32 out_capacity) {
    if (!kek || !wrapped || !out) return false;
    if (wrapped_length < 16 || wrapped_length % 8 || out_capacity + 8 != wrapped_length) return false;
    u32 n = wrapped_length / 8 - 1;
    if (n > 4) return false;
    u32 rk[44]; wifi_aes_expand(kek, rk);
    u8 a[8]; copy_bytes(a, wrapped, 8);
    u8 data[32];
    for (u32 i = 0; i < n * 8; i++) data[i] = wrapped[8 + i];
    for (u32 j = 6; j-- > 0;) {
        for (u32 i = n; i >= 1; i--) {
            u64 t = (u64)(n * j + i);
            for (u32 k = 0; k < 8; k++) a[k] ^= (u8)(t >> (56 - k * 8));
            u8 block[16];
            copy_bytes(block, a, 8);
            for (u32 k = 0; k < 8; k++) block[8 + k] = data[(i-1)*8 + k];
            wifi_aes_decrypt(rk, block);
            copy_bytes(a, block, 8);
            for (u32 k = 0; k < 8; k++) data[(i-1)*8 + k] = block[8 + k];
            if (i == 1) break;
        }
    }
    static const u8 iv[8] = { 0xa6,0xa6,0xa6,0xa6,0xa6,0xa6,0xa6,0xa6 };
    if (!bytes_equal(a, iv, 8)) return false;
    for (u32 i = 0; i < out_capacity; i++) out[i] = data[i];
    return true;
}
void wpa_tk(const wpa_t* hs, u8 out[WPA_TK_LEN]) {
    if (!hs || !out) return;
    copy_bytes(out, hs->ptk + WPA_KCK_LEN + WPA_KEK_LEN, WPA_TK_LEN);
}
bool wpa_completed(const wpa_t* hs) { return hs && hs->state == WPA_COMPLETED; }
