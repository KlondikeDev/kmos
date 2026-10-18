// This file has been generated in whole or in part by a Large Language Model
// WPA2-PSK 4-way handshake, supplicant side (802.11-2016 12.7.6).
// PTK = PRF-X(PMK, "Pairwise key expansion", MinMax(MACs) || MinMax(Nonces)).
// KCK verifies authenticated AP messages (HMAC-SHA1-128); M1 has no MIC.
// GTKs are accepted only inside encrypted, authenticated KDEs. Repeated
// messages are acknowledged without reinstalling keys or resetting PNs.
// The SNonce requires hardware entropy; failure leaves the state unchanged.
// Protocol reference: OpenBSD sys/net80211/ieee80211_pae_{input,output}.c.
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
static void zero_bytes(u8* p, u32 length) {
    volatile u8* out=p;
    for (u32 i=0;i<length;i++) out[i]=0;
}
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
const u8 wpa_rsn_ie[WPA_RSN_IE_LEN] = {
    WPA_RSN_IE_BYTES
};
void wpa_reset(wpa_t* hs) {
    if (!hs) return;
    volatile u8* p = (volatile u8*)hs;
    for (u32 i = 0; i < sizeof(*hs); i++) p[i] = 0;
}
void wpa_start(wpa_t* hs, const u8 ap_mac[6], const u8 sta_mac[6]) {
    if (!hs) return;
    u8 pmk[WPA_PMK_LEN];
    copy_bytes(pmk, hs->pmk, sizeof(pmk));
    wpa_reset(hs);
    copy_bytes(hs->pmk, pmk, sizeof(pmk));
    zero_bytes(pmk, sizeof(pmk));
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
    u8 scratch[512];
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
// Validate an AP RSN advertisement: group CCMP, pairwise CCMP and PSK
// available, no mandatory management protection (we do not implement PMF).
static bool rsn_supported(const u8* ie, u32 length) {
    static const u8 ccmp[4] = {0,0x0f,0xac,4};
    static const u8 psk[4] = {0,0x0f,0xac,2};
    if (length < 20 || ie[0] != 48 || (u32)ie[1]+2 != length ||
        ie[2] != 1 || ie[3] || !bytes_equal(ie+4,ccmp,4)) return false;
    u32 pos = 8;
    u32 count = (u32)ie[pos] | ((u32)ie[pos+1] << 8); pos += 2;
    if (!count || count > (length-pos)/4) return false;
    bool found = false;
    for (u32 i=0;i<count;i++,pos+=4) found |= bytes_equal(ie+pos,ccmp,4);
    if (!found || pos+2 > length) return false;
    count = (u32)ie[pos] | ((u32)ie[pos+1] << 8); pos += 2;
    if (!count || count > (length-pos)/4) return false;
    found = false;
    for (u32 i=0;i<count;i++,pos+=4) found |= bytes_equal(ie+pos,psk,4);
    if (!found) return false;
    if (pos == length) return true;
    if (pos+2 > length || (ie[pos] & 0x40)) return false;
    pos += 2;
    if (pos == length) return true;
    if (pos+2 > length) return false;
    count = (u32)ie[pos] | ((u32)ie[pos+1] << 8); pos += 2;
    if (count > (length-pos)/16) return false;
    pos += count*16;
    return pos == length || pos+4 == length;
}
bool wpa_set_ap_rsn(wpa_t* hs, const void* ie, u32 length) {
    if (!hs || !ie || length > sizeof(hs->ap_rsn) || !rsn_supported(ie,length)) return false;
    copy_bytes(hs->ap_rsn,ie,length);
    hs->ap_rsn_length = (u16)length;
    return true;
}
static void make_reply(wpa_t* hs, const u8* key, u8* out, u16 info,
                       bool message2, u32* reply_length) {
    u32 n = KEY_DATA + (message2 ? WPA_RSN_IE_LEN : 0);
    zero_bytes(out,n);
    out[0] = key[0]; out[1] = 3; out[4] = WPA_KEYDESC_EAPOL;
    write16(out+2,(u16)(n-4));
    write16(out+KEY_INFO,info);
    copy_bytes(out+KEY_REPLAY,key+KEY_REPLAY,WPA_REPLAY_LEN);
    if (message2) {
        copy_bytes(out+KEY_NONCE,hs->snonce,WPA_NONCE_LEN);
        write16(out+KEY_DATA_LENGTH,WPA_RSN_IE_LEN);
        copy_bytes(out+KEY_DATA,wpa_rsn_ie,WPA_RSN_IE_LEN);
    }
    fill_mic(hs,out,n);
    if (reply_length) *reply_length=n;
}
// Parse authenticated RSN key data, including the vendor GTK KDE and
// RFC3394's dd/zero padding. Never treat the entire plaintext as raw GTK.
static bool parse_key_data(const wpa_t* hs, const u8* data, u32 length,
                            bool pairwise, u8 gtk[16], u8* index, bool* has_gtk) {
    bool rsn_seen=false;
    *has_gtk=false;
    for (u32 pos=0;pos<length;) {
        if (data[pos]==0xdd && (pos+1==length || data[pos+1]==0)) {
            for (u32 i=pos+1;i<length;i++) if (data[i]) return false;
            break;
        }
        if (length-pos<2 || (u32)data[pos+1]+2>length-pos) return false;
        u32 size=(u32)data[pos+1]+2;
        if (data[pos]==48) {
            if (rsn_seen || !rsn_supported(data+pos,size)) return false;
            if (hs->ap_rsn_length && (size!=hs->ap_rsn_length ||
                !bytes_equal(data+pos,hs->ap_rsn,size))) return false;
            rsn_seen=true;
        } else if (data[pos]==0xdd && size>=6 && data[pos+2]==0 &&
                   data[pos+3]==0x0f && data[pos+4]==0xac && data[pos+5]==1) {
            if (*has_gtk || size!=24 || data[pos+6]&0xf8 || data[pos+7]) return false;
            *has_gtk=true; *index=data[pos+6]&3;
            copy_bytes(gtk,data+pos+8,16);
        }
        pos+=size;
    }
    return pairwise ? rsn_seen : *has_gtk;
}
bool wpa_receive(wpa_t* hs, const void* frame, u32 length,
                 void* reply, u32 reply_capacity, u32* reply_length) {
    if (reply_length) *reply_length=0;
    if (!hs || !frame || length<KEY_DATA || hs->state==WPA_DISCONNECTED) return false;
    const u8* key=frame;
    u32 pdu_length=(u32)read16(key+2)+4;
    if (key[0]<1 || key[0]>3 || key[1]!=3 || key[4]!=WPA_KEYDESC_EAPOL ||
        pdu_length<KEY_DATA || pdu_length>length || pdu_length>512) return false;
    length=pdu_length; // Ignore Ethernet padding beyond the authenticated PDU.
    u16 info=read16(key+KEY_INFO), data_length=read16(key+KEY_DATA_LENGTH);
    if (KEY_DATA+(u32)data_length!=length || (info&7)!=WPA_KEYINFO_VERSION ||
        (info & ~(2u|WPA_KEYINFO_KEY|WPA_KEYINFO_INSTALL|WPA_KEYINFO_ACK|
                   WPA_KEYINFO_MIC|WPA_KEYINFO_SECURE|WPA_KEYINFO_ENCRYPTED)) ||
        !(info&WPA_KEYINFO_ACK) || read16(key+KEY_LENGTH)!=16) return false;
    bool pairwise=(info&WPA_KEYINFO_KEY)!=0;
    bool mic=(info&WPA_KEYINFO_MIC)!=0;
    if (!mic) {
        if (info!=(2|WPA_KEYINFO_KEY|WPA_KEYINFO_ACK)) return false;
        if (hs->state!=WPA_AUTH_ASSOCIATED &&
            !replay_newer(key+KEY_REPLAY,hs->replay) &&
            !(hs->state==WPA_MSG1_SEEN && bytes_equal(key+KEY_REPLAY,hs->replay,8))) return true;
        if (!reply || reply_capacity<KEY_DATA+WPA_RSN_IE_LEN) return true;
        if (hs->state==WPA_MSG1_SEEN && bytes_equal(key+KEY_REPLAY,hs->replay,8) &&
            !bytes_equal(hs->anonce,key+KEY_NONCE,32)) return true;
        wpa_t next=*hs;
        bool reuse=hs->state==WPA_MSG1_SEEN && bytes_equal(hs->anonce,key+KEY_NONCE,32);
        if (!reuse) {
            if (random_read(next.snonce,WPA_NONCE_LEN)!=WPA_NONCE_LEN) {
                wpa_reset(&next);
                return true;
            }
            copy_bytes(next.anonce,key+KEY_NONCE,32);
            derive_ptk(&next);
        }
        copy_bytes(next.replay,key+KEY_REPLAY,8);
        next.state=WPA_MSG1_SEEN;
        make_reply(&next,key,reply,2|WPA_KEYINFO_KEY|WPA_KEYINFO_MIC,true,reply_length);
        *hs=next;
        wpa_reset(&next);
        return true;
    }
    if (hs->state!=WPA_MSG1_SEEN && hs->state!=WPA_COMPLETED) return true;
    if (!(info&WPA_KEYINFO_SECURE) ||
        (pairwise ? !(info&WPA_KEYINFO_INSTALL) : (info&WPA_KEYINFO_INSTALL))) return false;
    bool newer=replay_newer(key+KEY_REPLAY,hs->replay);
    bool equal=bytes_equal(key+KEY_REPLAY,hs->replay,8);
    if (!newer && !(equal && hs->state==WPA_COMPLETED)) return true;
    if (pairwise && !bytes_equal(key+KEY_NONCE,hs->anonce,32)) return true;
    if (!pairwise && hs->state!=WPA_COMPLETED) return true;
    if (!verify_mic(hs,key,length)) return true;
    u8 plain[416], gtk[16], index=0;
    bool has_gtk;
    u32 plain_length=data_length;
    if (info&WPA_KEYINFO_ENCRYPTED) {
        if (data_length<24 || data_length>sizeof(plain) || data_length%8) return true;
        plain_length=data_length-8;
        if (!wpa_unwrap(hs->ptk+WPA_KCK_LEN,key+KEY_DATA,data_length,plain,plain_length)) return true;
    } else {
        if (data_length>sizeof(plain)) return true;
        copy_bytes(plain,key+KEY_DATA,data_length);
    }
    if (!parse_key_data(hs,plain,plain_length,pairwise,gtk,&index,&has_gtk) ||
        (has_gtk && !(info&WPA_KEYINFO_ENCRYPTED))) return true;
    // A retransmitted M3 can only confirm the installed keys, never replace
    // them or reset their receive/transmit packet counters (KRACK).
    if (hs->state==WPA_COMPLETED && pairwise && has_gtk &&
        (!hs->gtk_ok || index!=hs->gtk_index || !bytes_equal(gtk,hs->gtk,16))) return true;
    if (equal && has_gtk && (!hs->gtk_ok || index!=hs->gtk_index ||
                            !bytes_equal(gtk,hs->gtk,16))) return true;
    if (!reply || reply_capacity<KEY_DATA) return true;
    if (has_gtk && (!hs->gtk_ok || index!=hs->gtk_index || !bytes_equal(gtk,hs->gtk,16))) {
        copy_bytes(hs->gtk,gtk,16); hs->gtk_index=index; hs->gtk_ok=true;
        hs->gtk_rsc=0;
        for (u32 i=0;i<6;i++) hs->gtk_rsc|=(u64)key[KEY_RSC+i]<<(8*i);
        hs->gtk_generation++;
    }
    if (hs->state==WPA_MSG1_SEEN && (!hs->ptk_generation ||
        !bytes_equal(hs->installed_tk,hs->ptk+WPA_KCK_LEN+WPA_KEK_LEN,WPA_TK_LEN))) {
        copy_bytes(hs->installed_tk,hs->ptk+WPA_KCK_LEN+WPA_KEK_LEN,WPA_TK_LEN);
        hs->ptk_generation++;
        hs->tx_pn=0; hs->rx_pn=0;
    }
    copy_bytes(hs->replay,key+KEY_REPLAY,8);
    make_reply(hs,key,reply,2|WPA_KEYINFO_MIC|WPA_KEYINFO_SECURE|
               (pairwise?WPA_KEYINFO_KEY:0),false,reply_length);
    hs->state=WPA_COMPLETED;
    zero_bytes(plain,sizeof(plain)); zero_bytes(gtk,sizeof(gtk));
    return true;
}
// AES key unwrap (RFC 3394) with the KEK: 8-byte IV check, n 64-bit blocks.
bool wpa_unwrap(const u8* kek, const u8* wrapped, u32 wrapped_length,
                u8* out, u32 out_capacity) {
    if (!kek || !wrapped || !out) return false;
    if (wrapped_length < 24 || wrapped_length % 8 || out_capacity != wrapped_length - 8) return false;
    u32 n = wrapped_length / 8 - 1;
    if (n > 51) return false;
    u32 rk[44]; wifi_aes_expand(kek, rk);
    u8 a[8]; copy_bytes(a, wrapped, 8);
    u8 data[408];
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
    bool valid=bytes_equal(a,iv,8);
    if (valid) for (u32 i=0;i<out_capacity;i++) out[i]=data[i];
    zero_bytes((u8*)rk,sizeof(rk)); zero_bytes(data,sizeof(data));
    return valid;
}
void wpa_tk(const wpa_t* hs, u8 out[WPA_TK_LEN]) {
    if (!hs || !out) return;
    copy_bytes(out, hs->ptk + WPA_KCK_LEN + WPA_KEK_LEN, WPA_TK_LEN);
}
bool wpa_completed(const wpa_t* hs) { return hs && hs->state == WPA_COMPLETED; }
