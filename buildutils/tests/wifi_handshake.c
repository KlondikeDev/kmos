// This file has been generated in whole or in part by a Large Language Model
// Independent AP fixtures use Python hashlib/hmac and OpenSSL AES-wrap;
// no expected key or MIC is derived by the implementation under test.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../include/wifi_wpa.h"
#include "wifi_wpa_vectors.h"
static bool entropy_ok = true;
static u32 entropy_calls;
static u8 entropy_base=0x5a;
i64 random_read(void* p, u64 n) {
    entropy_calls++;
    if (!entropy_ok) return 0;
    for (u64 i = 0; i < n; i++) ((u8*)p)[i] = (u8)(entropy_base+i);
    return (i64)n;
}
static const u8 ap[6] = {0,0x1a,0x2b,0x3c,0x4d,0x5e};
static const u8 sta[6] = {2,0x11,0x22,0x33,0x44,0x55};
static u8 reply[512];
static u32 reply_length;
static void receive(wpa_t* hs, const u8* p, u32 n) {
    assert(wpa_receive(hs,p,n,reply,sizeof(reply),&reply_length));
}
static void begin(wpa_t* hs) {
    memset(hs,0,sizeof(*hs));
    wpa_derive_pmk(hs,"password","IEEE",4);
    assert(!memcmp(hs->pmk,v_pmk,32));
    wpa_start(hs,ap,sta);
    assert(!memcmp(hs->pmk,v_pmk,32));
}
int main(void) {
    // RFC3394 section 4.1 independent 128-bit KEK/key-data vector.
    const u8 kek[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    const u8 wrap[24]={0x1f,0xa6,0x8b,0x0a,0x81,0x12,0xb4,0x47,
        0xae,0xf3,0x4b,0xd8,0xfb,0x5a,0x7b,0x82,0x9d,0x3e,0x86,0x23,0x71,0xd2,0xcf,0xe5};
    const u8 raw[16]={0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    u8 unwrapped[16];
    assert(wpa_unwrap(kek,wrap,sizeof(wrap),unwrapped,sizeof(unwrapped)));
    assert(!memcmp(unwrapped,raw,16));
    wpa_t hs,before;
    begin(&hs);
    // Reject mandatory PMF and TKIP group cipher before association.
    u8 rsn[sizeof(v_rsn)]; memcpy(rsn,v_rsn,sizeof(rsn)); rsn[20]=0x40;
    assert(!wpa_set_ap_rsn(&hs,rsn,sizeof(rsn)));
    memcpy(rsn,v_rsn,sizeof(rsn)); rsn[7]=2;
    assert(!wpa_set_ap_rsn(&hs,rsn,sizeof(rsn)));
    assert(wpa_set_ap_rsn(&hs,v_rsn,sizeof(v_rsn)));
    receive(&hs,v_m1,sizeof(v_m1));
    assert(reply_length==sizeof(v_m2) && !memcmp(reply,v_m2,sizeof(v_m2)));
    assert(!memcmp(hs.ptk,v_ptk,64));
    u32 calls=entropy_calls;
    receive(&hs,v_m1,sizeof(v_m1)); // lost M2: stable SNonce and identical reply
    assert(entropy_calls==calls && reply_length==sizeof(v_m2));
    assert(!memcmp(reply,v_m2,sizeof(v_m2)));
    before=hs;
    u8 bad[512]; memcpy(bad,v_m3,sizeof(v_m3)); bad[81]^=1;
    receive(&hs,bad,sizeof(v_m3));
    assert(!reply_length && !memcmp(&hs,&before,sizeof(hs)));
    receive(&hs,v_wrong_nonce,sizeof(v_wrong_nonce));
    assert(!reply_length && !memcmp(&hs,&before,sizeof(hs)));
    receive(&hs,v_bad_kde,sizeof(v_bad_kde));
    assert(!reply_length && !memcmp(&hs,&before,sizeof(hs)));
    receive(&hs,v_rsn_mismatch,sizeof(v_rsn_mismatch));
    assert(!reply_length && !memcmp(&hs,&before,sizeof(hs)));
    receive(&hs,v_no_rsn,sizeof(v_no_rsn));
    assert(!reply_length && !memcmp(&hs,&before,sizeof(hs)));
    receive(&hs,v_m3,sizeof(v_m3));
    assert(wpa_completed(&hs) && hs.gtk_ok && hs.gtk_index==2);
    assert(!memcmp(hs.gtk,v_gtk,16));
    assert(reply_length==sizeof(v_m4) && !memcmp(reply,v_m4,sizeof(v_m4)));
    hs.tx_pn=88; hs.rx_pn=77;
    receive(&hs,v_m3,sizeof(v_m3)); // lost M4: acknowledge without reinstall
    assert(reply_length==sizeof(v_m4) && !memcmp(reply,v_m4,sizeof(v_m4)));
    assert(hs.tx_pn==88 && hs.rx_pn==77);
    receive(&hs,v_m3_retry,sizeof(v_m3_retry));
    assert(reply_length==sizeof(v_m4_retry) && !memcmp(reply,v_m4_retry,sizeof(v_m4_retry)));
    assert(hs.tx_pn==88 && hs.rx_pn==77);
    u32 generation=hs.gtk_generation;
    receive(&hs,v_group,sizeof(v_group));
    assert(hs.gtk_index==1 && !memcmp(hs.gtk,v_gtk2,16));
    assert(reply_length==sizeof(v_group_ack) && !memcmp(reply,v_group_ack,sizeof(v_group_ack)));
    assert(hs.gtk_generation==generation+1);
    generation=hs.gtk_generation; hs.gtk_rsc=123;
    receive(&hs,v_group_retry,sizeof(v_group_retry));
    assert(hs.gtk_generation==generation && hs.gtk_rsc==123);
    receive(&hs,v_m3,sizeof(v_m3)); assert(!reply_length); // stale replay
    // Pairwise rekey installs fresh keys once; retries preserve per-key PNs.
    assert(hs.ptk_generation==1);
    entropy_base=0x5b;
    receive(&hs,v_rekey_m1,sizeof(v_rekey_m1));
    assert(!wpa_completed(&hs) && hs.ptk_generation==1);
    assert(reply_length==sizeof(v_rekey_m2) && !memcmp(reply,v_rekey_m2,sizeof(v_rekey_m2)));
    assert(!memcmp(hs.ptk,v_rekey_ptk,64));
    receive(&hs,v_rekey_m3,sizeof(v_rekey_m3));
    assert(wpa_completed(&hs) && hs.ptk_generation==2 && !hs.tx_pn && !hs.rx_pn);
    assert(reply_length==sizeof(v_rekey_m4) && !memcmp(reply,v_rekey_m4,sizeof(v_rekey_m4)));
    hs.tx_pn=91; hs.rx_pn=92;
    receive(&hs,v_rekey_m3,sizeof(v_rekey_m3));
    assert(hs.ptk_generation==2 && hs.tx_pn==91 && hs.rx_pn==92);
    entropy_base=0x5a;
    // Even an accidental identical candidate TK must not reinstall/reset it.
    begin(&hs); receive(&hs,v_m1,sizeof(v_m1)); receive(&hs,v_m3,sizeof(v_m3));
    hs.tx_pn=93; hs.rx_pn=94;
    receive(&hs,v_rekey_m1,sizeof(v_rekey_m1));
    receive(&hs,v_same_key_m3,sizeof(v_same_key_m3));
    assert(wpa_completed(&hs) && hs.ptk_generation==1 && hs.tx_pn==93 && hs.rx_pn==94);
    // EAPOL header truncation, unsupported descriptor version, missing ACK.
    begin(&hs); before=hs;
    memcpy(bad,v_m1,sizeof(v_m1)); bad[3]--;
    assert(!wpa_receive(&hs,bad,sizeof(v_m1),reply,sizeof(reply),&reply_length));
    assert(!memcmp(&hs,&before,sizeof(hs)));
    memcpy(bad,v_m1,sizeof(v_m1)); bad[6]=0x89;
    assert(!wpa_receive(&hs,bad,sizeof(v_m1),reply,sizeof(reply),&reply_length));
    memcpy(bad,v_m1,sizeof(v_m1)); bad[6]=0x0a;
    assert(!wpa_receive(&hs,bad,sizeof(v_m1),reply,sizeof(reply),&reply_length));
    // Ethernet padding is excluded by the EAPOL length, version 1 accepted.
    memcpy(bad,v_m1,sizeof(v_m1)); memset(bad+sizeof(v_m1),0,12); bad[0]=1;
    receive(&hs,bad,sizeof(v_m1)+12); assert(reply_length==sizeof(v_m2) && reply[0]==1);
    // AP retransmits M1 with an incremented replay counter: keep SNonce.
    begin(&hs);
    receive(&hs,v_m1,sizeof(v_m1)); calls=entropy_calls;
    receive(&hs,v_m1_retry,sizeof(v_m1_retry));
    assert(entropy_calls==calls && reply_length==sizeof(v_m2_retry));
    assert(!memcmp(reply,v_m2_retry,sizeof(v_m2_retry)));
    receive(&hs,v_m3_retry,sizeof(v_m3_retry));
    assert(wpa_completed(&hs));
    // All truncated fixed headers are rejected without state mutation.
    begin(&hs); before=hs;
    for (u32 i=0;i<99;i++) {
        assert(!wpa_receive(&hs,v_m1,i,reply,sizeof(reply),&reply_length));
        assert(!reply_length && !memcmp(&hs,&before,sizeof(hs)));
    }
    begin(&hs); before=hs; entropy_ok=false;
    receive(&hs,v_m1,sizeof(v_m1));
    assert(!reply_length && !memcmp(&hs,&before,sizeof(hs)));
    wpa_reset(&hs);
    memset(&before,0,sizeof(before));
    assert(!memcmp(&hs,&before,sizeof(hs)));
    puts("WPA2 independent AP fixtures, replay, MIC, KDE and entropy: PASS");
    return 0;
}
