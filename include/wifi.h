// This file has been generated in whole or in part by a Large Language Model
// Minimal 802.11 station core: frame parsing/building plus the open-system
// authentication and association state machine. No hardware access here; the
// driver feeds received management frames in and pulls transmit frames out.
// Data frames convert to Ethernet for the IP stack. WPA2 key exchange and
// CCMP arrive separately; this file only reaches the associated state.
#pragma once
#include <types.h>
#define WIFI_ADDR_LEN 6
#define WIFI_SSID_MAX 32
#define WIFI_FRAME_MAX 2312
// Management frame types (type 0).
#define WIFI_MGMT_ASSOC_REQ 0x00
#define WIFI_MGMT_ASSOC_RESP 0x01
#define WIFI_MGMT_AUTH 0x0b
#define WIFI_MGMT_DEAUTH 0x0c
// Information element IDs.
#define WIFI_IE_SSID 0
#define WIFI_IE_RATES 1
#define WIFI_IE_DS_PARAM 3
#define WIFI_IE_XRATES 50
// Fixed field sizes.
#define WIFI_CAPAB_LEN 2
#define WIFI_LISTEN_LEN 2
#define WIFI_AUTH_FIXED 6
#define WIFI_ASSOC_RESP_FIXED 6
typedef enum {
    WIFI_IDLE = 0,
    WIFI_AUTH_SENT,
    WIFI_AUTH_OK,
    WIFI_ASSOC_SENT,
    WIFI_ASSOCIATED,
} wifi_state_t;
// A parsed 802.11 header sufficient for management handling.
typedef struct {
    u8 type, subtype;
    bool to_ds, from_ds, protected;
    u8 addr1[WIFI_ADDR_LEN];
    u8 addr2[WIFI_ADDR_LEN];
    u8 addr3[WIFI_ADDR_LEN];
    u16 sequence;
    const u8* body;
    u32 body_length;
} wifi_frame_t;
// Station context: caller owns the storage, no allocation inside.
typedef struct {
    wifi_state_t state;
    u8 local[WIFI_ADDR_LEN];
    u8 bssid[WIFI_ADDR_LEN];
    u8 ssid[WIFI_SSID_MAX];
    u32 ssid_length;
    u16 sequence;
    bool wpa2;
    u8 rates[16];
    u8 rates_length;
    u16 aid;
    u16 last_status; // status of the last auth/assoc response (diagnostics)
    u16 last_tx_length;
    u8 last_tx[WIFI_FRAME_MAX];
} wifi_sta_t;
void wifi_sta_init(wifi_sta_t* sta, const u8 local[WIFI_ADDR_LEN]);
// Configure the target network. False on a bad SSID length or state.
bool wifi_sta_join(wifi_sta_t* sta, const u8 bssid[WIFI_ADDR_LEN],
                   const void* ssid, u32 ssid_length);
// Configure before association: WPA2-PSK CCMP RSN IE and Privacy bit.
void wifi_sta_set_wpa2(wifi_sta_t* sta, bool enabled);
// Copy supported AP rates from scan IEs, including basic-rate markings.
bool wifi_sta_set_rates(wifi_sta_t* sta, const u8* rates, u32 length);
// Parse a raw frame; false when shorter than a header or an invalid version.
bool wifi_parse(const void* frame, u32 length, wifi_frame_t* out);
// Build the next transmit frame for the current state (auth, then assoc).
// Returns the frame length, 0 when nothing is due.
u32 wifi_next_tx(wifi_sta_t* sta);
// Consume a received frame; advances the state machine. Returns true when
// the frame belonged to the current exchange (even on rejection).
bool wifi_receive(wifi_sta_t* sta, const void* frame, u32 length);
// Convert an associated plaintext data frame (decrypt CCMP first) to Ethernet.
// Returns the Ethernet length, 0 when not a data frame for us.
u32 wifi_to_ethernet(const wifi_sta_t* sta, const void* frame, u32 length,
                     void* ethernet, u32 capacity);
