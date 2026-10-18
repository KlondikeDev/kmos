// This file has been generated in whole or in part by a Large Language Model
// AX200 station and passive UMAC scan. All channel permissions come from
// NVM intersected with the firmware MCC response; no guessed regulatory map.
#pragma once
#include <types.h>
#define IWX_STATION_RESULTS 32u
#define IWX_STATION_CHANNELS 51u
#define IWX_STATION_FRAME_MAX 2312u
typedef enum { IWX_SECURITY_OPEN, IWX_SECURITY_WPA2_PSK_CCMP, IWX_SECURITY_UNSUPPORTED } iwx_security_t;
typedef struct {
 u8 ssid[32], ssid_length, bssid[6], channel;
 i32 rssi_dbm;
 iwx_security_t security;
 u16 capability, beacon_interval;
 u8 dtim_period, dtim_count;
 u64 tsf;
 u32 device_time;
 u8 rates[12], rate_count;
 u8 rsn[257];
 u16 rsn_length;
} iwx_scan_result_t;
// Invoked after firmware PHY configuration and NVM initialization. The
// profile array uses Intel's fixed AX200 NVM channel index order.
i32 iwx_station_configure(const u8 mac[6], u32 tx_chains, u32 rx_chains,
                          const u32* profiles, u32 channel_count);
i32 iwx_station_scan(void);
bool iwx_station_scanning(void);
u32 iwx_station_scan_results(iwx_scan_result_t* output, u32 capacity);
void iwx_station_notification(u8 group, u8 opcode, const u8* data, u32 length);
void iwx_station_poll(void);
i32 iwx_station_connect(const void* ssid, u32 length, const char* passphrase);
void iwx_station_disconnect(void);
typedef struct { u32 line, extra; i32 error; u32 wifi_state, link, last_status; } iwx_station_diag_t;
void iwx_station_diag(iwx_station_diag_t* out);
bool iwx_station_send(const void* frame, u32 length);
u32 iwx_station_receive(void* frame, u32 capacity);
// Pure wire helpers, usable by host protocol tests and by the data DMA ring.
bool iwx_station_build_tx(void* prefix, u32 prefix_capacity, const void* frame,
                          u32 length, u8 tx_antenna, bool band_2ghz, u8 rate_version);
bool iwx_station_parse_beacon(const void* frame, u32 length, u8 channel,
                              i32 rssi, iwx_scan_result_t* result);
u32 iwx_station_decode_rx(const void* payload, u32 length, void* frame,
                          u32 capacity, u8* channel, i32* rssi);
