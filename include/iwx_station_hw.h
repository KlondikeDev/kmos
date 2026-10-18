// This file has been generated in whole or in part by a Large Language Model
/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************
 */


// AX200 wire layouts adapted from OpenBSD if_iwxreg.h 1.75.
#pragma once
#include <types.h>
struct iwx_station_tx_cmd {
 u16 len, offload_assist;
 u32 flags;
 u32 pn_low;
 u16 pn_high, aux_info;
 u32 rate_n_flags;
} PACKED;
struct iwx_station_rx_desc {
 u16 mpdu_len;
 u8 mac_flags1, mac_flags2, amsdu_info;
 u16 phy_info;
 u8 mac_phy_idx;
 u16 raw_csum, l3l4_flags;
 u32 status, reorder_data;
 u32 rss_hash, filter_match, rate_n_flags;
 u8 energy_a, energy_b, channel, mac_context;
 u32 gp2_on_air_rise;
 u64 tsf_on_air_rise;
} PACKED;
struct iwx_station_scan_config { u8 cam, promiscuous, bcast_sta, reserved; u32 tx_chains, rx_chains; } PACKED;
struct iwx_station_probe_segment { u16 offset, len; } PACKED;
struct iwx_station_probe {
 struct iwx_station_probe_segment mac_header, band_data[3], common_data;
 u8 buf[512];
} PACKED;
struct iwx_station_scan_general {
 u16 flags;
 u8 reserved, scan_start_mac_id, active_dwell[2];
 u8 adwell_default_2g, adwell_default_5g, adwell_default_social, reserved1;
 u16 adwell_max_budget;
 u32 max_out_of_time[2], suspend_time[2], scan_priority;
 u8 passive_dwell[2], num_of_fragments[2];
} PACKED;
struct iwx_station_scan_channel { u32 flags; u8 channel, band, iterations, interval; } PACKED;
struct iwx_station_scan_schedule { u16 interval; u8 iterations, reserved; } PACKED;
struct iwx_station_ssid { u8 id, length, ssid[32]; } PACKED;
// Firmware 71 advertises version 15. OpenBSD iwx_umac_scan_v14 uses this
// exact v14 layout for it; only versions 14/15 are accepted here.
struct iwx_station_scan_req {
 u32 uid, priority;
 struct iwx_station_scan_general general;
 u8 channel_flags, channel_count, n_aps_override[2];
 struct iwx_station_scan_channel channels[67];
 struct iwx_station_scan_schedule schedules[2];
 u16 delay, reserved;
 struct iwx_station_probe probe;
 u8 short_ssid_count, bssid_count;
 u16 reserved2;
 struct iwx_station_ssid ssids[20];
 u32 short_ssid[8];
 u8 bssids[16][6];
} PACKED;
_Static_assert(sizeof(struct iwx_station_tx_cmd) == 20, "AX200 TX prefix");
_Static_assert(sizeof(struct iwx_station_rx_desc) == 48, "AX200 RX v1 descriptor");
_Static_assert(sizeof(struct iwx_station_scan_config) == 12, "scan config v5");
_Static_assert(sizeof(struct iwx_station_scan_general) == 36, "scan general v10");
_Static_assert(sizeof(struct iwx_station_scan_req) == 1940, "scan request v14/v15");
_Static_assert(__builtin_offsetof(struct iwx_station_scan_req, channels) == 48, "scan channels");
_Static_assert(__builtin_offsetof(struct iwx_station_scan_req, probe) == 596, "scan probe");
_Static_assert(__builtin_offsetof(struct iwx_station_rx_desc, status) == 12, "RX status");
_Static_assert(__builtin_offsetof(struct iwx_station_rx_desc, channel) == 34, "RX channel");
struct iwx_station_phy_context {
 u32 id_and_color, action;
 u8 band, channel, width, ctrl_pos;
 u32 lmac_id, rxchain_info, dsp_cfg_flags, reserved;
} PACKED;
struct iwx_station_binding { u32 id_and_color, action, macs[3], phy, lmac; } PACKED;
struct iwx_station_ac_qos { u16 cw_min,cw_max;u8 aifsn,fifos_mask;u16 txop; } PACKED;
struct iwx_station_mac_data {
 u32 is_assoc,dtim_time;
 u64 dtim_tsf;
 u32 bi,reserved1,dtim_interval,data_policy,listen_interval,assoc_id,assoc_beacon_arrive_time;
} PACKED;
struct iwx_station_mac_context {
 u32 id_and_color,action,mac_type,tsf_id;
 u8 node[6];u16 reserved_node;
 u8 bssid[6];u16 reserved_bssid;
 u32 cck_rates,ofdm_rates,protection_flags,short_preamble,short_slot,filter_flags,qos_flags;
 struct iwx_station_ac_qos ac[5];
 union {struct iwx_station_mac_data sta;u8 mac_specific[48];};
} PACKED;
struct iwx_station_add_sta {
 u8 add_modify,awake_acs;u16 tid_disable_tx;
 u32 mac_id_color;
 u8 addr[6];u16 reserved2;
 u8 sta_id,modify_mask;u16 reserved3;
 u32 flags,flags_mask;
 u8 add_ba_tid,remove_ba_tid;u16 add_ba_ssn;
 u16 sleep_tx_count;u8 sleep_state_flags,station_type;
 u16 assoc_id,beamform_flags;
 u32 tfd_queue_mask;
 u16 rx_ba_window;u8 sp_length,uapsd_acs;
} PACKED;
struct iwx_station_session {u32 id_color,action,conf_id,duration_tu,repetition_count,interval;} PACKED;
// RLC_CONFIG_CMD v2 (DATA_PATH group 5, opcode 8).
struct iwx_station_rlc_config {
 u32 phy_id, rx_chain_info, rlc_reserved;
 u32 chain_a_sad_mode, chain_b_sad_mode, sad_mac_id, sad_reserved;
 u8 flags, reserved[3];
} PACKED;
_Static_assert(sizeof(struct iwx_station_rlc_config)==32,"RLC_CONFIG v2");
_Static_assert(sizeof(struct iwx_station_phy_context)==28,"PHY_CONTEXT v3");
// Same command when firmware has CAPA_ULTRA_HB_CHANNELS (48): Intel widened
// the embedded channel info (u32 channel) without bumping the command version.
struct iwx_station_phy_context_uhb {
 u32 id_and_color, action;
 u32 channel; u8 band, width, ctrl_pos, ci_reserved;
 u32 lmac_id, rxchain_info, dsp_cfg_flags, reserved;
} PACKED;
_Static_assert(sizeof(struct iwx_station_phy_context_uhb)==32,"PHY_CONTEXT v3 UHB");
_Static_assert(sizeof(struct iwx_station_binding)==28,"binding v2");
_Static_assert(sizeof(struct iwx_station_mac_context)==148,"MAC context v5");
_Static_assert(sizeof(struct iwx_station_add_sta)==48,"ADD_STA v12");
_Static_assert(sizeof(struct iwx_station_session)==24,"session protection v1");
_Static_assert(__builtin_offsetof(struct iwx_station_mac_context,sta)==100,"MAC station offset");
_Static_assert(__builtin_offsetof(struct iwx_station_add_sta,sta_id)==16,"station index offset");
