// This file has been generated in whole or in part by a Large Language Model
// 802.11 station core: open-system authentication then association. Frame
// control is little-endian on the air; sequence numbers count up per station.
// The state machine accepts only frames from the joined BSSID addressed to
// the station, and only the reply the current state waits for.
#include <wifi.h>
static void copy_addr(u8 out[6], const u8 in[6]) {
    for (u32 i = 0; i < 6; i++) out[i] = in[i];
}
static bool addr_equal(const u8 a[6], const u8 b[6]) {
    u8 diff = 0; for (u32 i = 0; i < 6; i++) diff |= a[i] ^ b[i];
    return !diff;
}
void wifi_sta_init(wifi_sta_t* sta, const u8 local[WIFI_ADDR_LEN]) {
    if (!sta) return;
    for (u32 i = 0; i < sizeof(*sta); i++) ((u8*)sta)[i] = 0;
    if (local) copy_addr(sta->local, local);
    sta->state = WIFI_IDLE;
}
bool wifi_sta_join(wifi_sta_t* sta, const u8 bssid[WIFI_ADDR_LEN],
                   const void* ssid, u32 ssid_length) {
    if (!sta || !bssid || (!ssid && ssid_length) || !ssid_length ||
        ssid_length > WIFI_SSID_MAX || sta->state != WIFI_IDLE) return false;
    copy_addr(sta->bssid, bssid);
    for (u32 i = 0; i < ssid_length; i++) sta->ssid[i] = ((const u8*)ssid)[i];
    sta->ssid_length = ssid_length;
    sta->sequence = 0;
    sta->state = WIFI_IDLE;
    return true;
}
bool wifi_parse(const void* frame, u32 length, wifi_frame_t* out) {
    if (!frame || !out || length < 24) return false;
    const u8* p = frame;
    u16 control = (u16)p[0] | ((u16)p[1] << 8);
    if ((control & 3) == 3) return false; // protocol version must be 0
    out->type = (u8)((control >> 2) & 3);
    out->subtype = (u8)((control >> 4) & 15);
    out->to_ds = (control & 0x100) != 0;
    out->from_ds = (control & 0x200) != 0;
    out->protected = (control & 0x4000) != 0;
    copy_addr(out->addr1, p + 4);
    copy_addr(out->addr2, p + 10);
    copy_addr(out->addr3, p + 16);
    out->sequence = (u16)(((u16)p[22] >> 4) | ((u16)p[23] << 4));
    out->body = p + 24;
    out->body_length = length - 24;
    return true;
}
// Frame control word for a management frame of the given subtype.
static u16 mgmt_control(u8 subtype) { return (u16)(subtype << 4); }
// Append an information element; returns the new offset, or 0 on overflow.
static u32 put_ie(u8* frame, u32 offset, u32 capacity, u8 id,
                  const void* data, u32 length) {
    if (offset + 2 + length > capacity) return 0;
    frame[offset] = id; frame[offset + 1] = (u8)length;
    for (u32 i = 0; i < length; i++) frame[offset + 2 + i] = ((const u8*)data)[i];
    return offset + 2 + length;
}
static void put16(u8* p, u16 value) { p[0] = (u8)value; p[1] = (u8)(value >> 8); }
// Management header: control, duration, addr1/2/3, sequence.
static u32 put_header(u8* frame, u8 subtype, const u8 a1[6], const u8 a2[6],
                      const u8 a3[6], u16 sequence) {
    put16(frame, mgmt_control(subtype));
    put16(frame + 2, 0);
    copy_addr(frame + 4, a1); copy_addr(frame + 10, a2); copy_addr(frame + 16, a3);
    frame[22] = (u8)((sequence << 4) & 0xf0); frame[23] = (u8)(sequence >> 4);
    return 24;
}
u32 wifi_next_tx(wifi_sta_t* sta) {
    if (!sta || sta->state == WIFI_ASSOCIATED) return 0;
    u8* frame = sta->last_tx;
    u32 offset;
    if (sta->state == WIFI_IDLE) {
        // Open-system authentication, sequence 1.
        offset = put_header(frame, WIFI_MGMT_AUTH, sta->bssid, sta->local, sta->bssid, sta->sequence++);
        if (offset + WIFI_AUTH_FIXED > sizeof(sta->last_tx)) return 0;
        put16(frame + offset, 0); // open algorithm
        put16(frame + offset + 2, 1); // sequence 1
        put16(frame + offset + 4, 0); // success (request)
        sta->last_tx_length = offset + WIFI_AUTH_FIXED;
        sta->state = WIFI_AUTH_SENT;
        return sta->last_tx_length;
    }
    if (sta->state == WIFI_AUTH_OK) {
        // Association request with our SSID and a basic rate set.
        static const u8 rates[] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
        offset = put_header(frame, WIFI_MGMT_ASSOC_REQ, sta->bssid, sta->local, sta->bssid, sta->sequence++);
        if (offset + WIFI_CAPAB_LEN + WIFI_LISTEN_LEN > sizeof(sta->last_tx)) return 0;
        put16(frame + offset, 0x0001); // ESS capability
        put16(frame + offset + 2, 10); // listen interval
        offset += WIFI_CAPAB_LEN + WIFI_LISTEN_LEN;
        offset = put_ie(frame, offset, sizeof(sta->last_tx), WIFI_IE_SSID, sta->ssid, sta->ssid_length);
        if (!offset) return 0;
        offset = put_ie(frame, offset, sizeof(sta->last_tx), WIFI_IE_RATES, rates, sizeof(rates));
        if (!offset) return 0;
        sta->last_tx_length = offset;
        sta->state = WIFI_ASSOC_SENT;
        return sta->last_tx_length;
    }
    return 0;
}
bool wifi_receive(wifi_sta_t* sta, const void* frame, u32 length) {
    if (!sta || !frame || !length) return false;
    wifi_frame_t parsed;
    if (!wifi_parse(frame, length, &parsed)) return false;
    if (parsed.type != 0) return false; // management only here
    if (!addr_equal(parsed.addr2, sta->bssid)) return false;
    if (!addr_equal(parsed.addr1, sta->local)) return false;
    if (sta->state == WIFI_AUTH_SENT && parsed.subtype == WIFI_MGMT_AUTH) {
        if (parsed.body_length < WIFI_AUTH_FIXED) return true;
        u16 algorithm = (u16)parsed.body[0] | ((u16)parsed.body[1] << 8);
        u16 sequence = (u16)parsed.body[2] | ((u16)parsed.body[3] << 8);
        u16 status = (u16)parsed.body[4] | ((u16)parsed.body[5] << 8);
        if (algorithm == 0 && sequence == 2 && status == 0) sta->state = WIFI_AUTH_OK;
        return true;
    }
    if (sta->state == WIFI_ASSOC_SENT && parsed.subtype == WIFI_MGMT_ASSOC_RESP) {
        if (parsed.body_length < WIFI_ASSOC_RESP_FIXED) return true;
        u16 status = (u16)parsed.body[2] | ((u16)parsed.body[3] << 8);
        if (status == 0) {
            sta->aid = (u16)(((u16)parsed.body[4] | ((u16)parsed.body[5] << 8)) & 0x3fff);
            sta->state = WIFI_ASSOCIATED;
        }
        return true;
    }
    if (parsed.subtype == WIFI_MGMT_DEAUTH) {
        sta->state = WIFI_IDLE;
        return true;
    }
    return false;
}
// Strip an 802.11 data header (from-DS, to us) into an Ethernet frame:
// destination, source, ethertype plus the LLC/SNAP payload skipped.
u32 wifi_to_ethernet(const wifi_sta_t* sta, const void* frame, u32 length,
                     void* ethernet, u32 capacity) {
    if (!sta || sta->state != WIFI_ASSOCIATED || !frame || !ethernet) return 0;
    wifi_frame_t parsed;
    if (!wifi_parse(frame, length, &parsed)) return 0;
    if (parsed.type != 2 || !parsed.from_ds || parsed.to_ds) return 0;
    if (!addr_equal(parsed.addr1, sta->local)) return 0;
    // LLC/SNAP header: AA-AA-03, OUI 00-00-00, then the ethertype.
    if (parsed.body_length < 8) return 0;
    const u8* body = parsed.body;
    if (body[0] != 0xaa || body[1] != 0xaa || body[2] != 0x03) return 0;
    if (body[3] || body[4] || body[5]) return 0;
    u32 payload = parsed.body_length - 8;
    if (14 + payload > capacity) return 0;
    u8* out = ethernet;
    copy_addr(out, parsed.addr1);
    copy_addr(out + 6, parsed.addr3);
    out[12] = body[6]; out[13] = body[7];
    for (u32 i = 0; i < payload; i++) out[14 + i] = body[8 + i];
    return 14 + payload;
}
