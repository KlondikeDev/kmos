// This file has been generated in whole or in part by a Large Language Model
// Intel Wi-Fi 6 AX200 (device family 22000, discrete) register and firmware
// interface. Values follow the BSD-licensed OpenBSD iwx reference; only the
// discrete AX200 path is implemented (no integrated CNVi, no gen3 families).
#pragma once
#include <types.h>
// PCI: discrete AX200 only. Other family-22000 IDs are integrated parts.
#define IWX_PCI_AX200 0x2723
// CSR registers (offsets into the 128 KiB memory BAR).
#define IWX_CSR_HW_IF_CONFIG 0x000
#define IWX_CSR_INT_COALESCE 0x004
#define IWX_CSR_INT 0x008
#define IWX_CSR_INT_MASK 0x00c
#define IWX_CSR_FH_INT_STATUS 0x010
#define IWX_CSR_GPIO_IN 0x018
#define IWX_CSR_RESET 0x020
#define IWX_CSR_GP_CNTRL 0x024
#define IWX_CSR_HW_REV 0x028
#define IWX_CSR_UCODE_DRV_GP1 0x054
#define IWX_CSR_UCODE_DRV_GP2 0x060
#define IWX_CSR_MBOX_SET 0x088
#define IWX_CSR_HW_RF_ID 0x09c
#define IWX_CSR_MAC_SHADOW_CTRL 0x0a8
#define IWX_CSR_LTR_LONG_VAL 0x0d4
#define IWX_CSR_GIO_CHICKEN 0x100
#define IWX_CSR_DBG_HPET_MEM 0x240
#define IWX_CSR_DBG_LINK_PMGMT 0x250
#define IWX_CSR_CTXT_INFO_BA 0x40
#define IWX_CSR_MAC_CSR_BASE 0x380
// HW_IF_CONFIG bits.
#define IWX_HW_IF_NIC_READY 0x00400000u
#define IWX_HW_IF_PREPARE 0x08000000u
#define IWX_HW_IF_HAP_WAKE_L1A 0x00080000u
// GP_CNTRL bits on the AX200 path.
#define IWX_GP_MAC_CLOCK_READY 0x00000001u
#define IWX_GP_INIT_DONE 0x00000004u
#define IWX_GP_MAC_ACCESS_REQ 0x00000008u
#define IWX_GP_GOING_TO_SLEEP 0x00000010u
#define IWX_GP_MAC_ACCESS_EN 0x00000001u
#define IWX_GP_HW_RFKILL_SW 0x08000000u
// Reset register bits.
#define IWX_RESET_SW_RESET 0x00000080u
#define IWX_RESET_MASTER_DISABLED 0x00000100u
#define IWX_RESET_STOP_MASTER 0x00000200u
#define IWX_RESET_LINK_PMGMT_DISABLED 0x80000000u
// Mailbox: tell the firmware the host is alive.
#define IWX_MBOX_OS_ALIVE 0x20u
// MAC address strap registers, relative to IWX_CSR_MAC_CSR_BASE.
#define IWX_MAC_STRAP0 0x08
#define IWX_MAC_STRAP1 0x0c
// Indirect peripheral access through the HBUS window.
#define IWX_HBUS_TARG_ADDR 0x400
#define IWX_HBUS_TARG_RADDR 0x40c
#define IWX_HBUS_TARG_WADDR 0x410
#define IWX_HBUS_TARG_WRPTR 0x460
#define IWX_HBUS_PRPH_RADDR 0x444
#define IWX_HBUS_PRPH_WADDR 0x448
#define IWX_HBUS_PRPH_WDATA 0x44c
#define IWX_HBUS_PRPH_RDATA 0x450
// Peripheral registers touched during init.
#define IWX_UREG_CPU_INIT_RUN 0xa05c44u
#define IWX_HPM_DEBUG 0xa03440u
#define IWX_HPM_PERSISTENCE (1u << 12)
#define IWX_PREG_WPROT_22000 0xa04d00u
#define IWX_WPROT_LOCKED (1u << 12)
// Deadlines in nanoseconds (TSC clock, progresses with IRQs off).
#define IWX_RESET_NS 100000000ULL
#define IWX_PREPARE_NS 150000000ULL
#define IWX_CLOCK_NS 25000000ULL
#define IWX_ACCESS_NS 150000000ULL
#define IWX_ALIVE_NS 1000000000ULL
// Init firmware runs from DRAM sections described by context info.
#define IWX_CTXT_TFD_LONG (1u << 8)
#define IWX_CTXT_RB_CB_POS 4
#define IWX_CTXT_RB_SIZE_POS 9
#define IWX_CTXT_RB_4K 0x4u
#define IWX_MQ_RX_TABLE 512u
#define IWX_TX_RING_COUNT 256u
#define IWX_CMD_QUEUE 0u
// Command transport: TFD entries, RX buffer descriptors, kick registers.
#define IWX_TFD_TBS 25u
#define IWX_CMD_MAX_PAYLOAD 316u
#define IWX_RX_RING_COUNT 512u
#define IWX_RX_BUFFER_SIZE 4096u
#define IWX_HBUS_WRPTR 0x460u
#define IWX_CMD_NS 1000000000ULL
// Firmware command groups and bring-up commands.
#define IWX_GRP_LEGACY 0u
#define IWX_GRP_LONG 1u
#define IWX_GRP_SYSTEM 2u
#define IWX_GRP_MAC_CONF 3u
#define IWX_GRP_PHY 4u
#define IWX_GRP_DATA_PATH 5u
#define IWX_GRP_REGULATORY 0xcu
#define IWX_CMD_ALIVE 0x1u
#define IWX_CMD_INIT_COMPLETE 0x4u
#define IWX_SYS_INIT_EXTENDED_CFG 0x03u
#define IWX_SYS_INIT_NVM (1u << 1)
#define IWX_REG_NVM_ACCESS_COMPLETE 0x00u
#define IWX_REG_NVM_GET_INFO 0x02u
#define IWX_CMD_FAILED (1u << 6)
// Run the post-alive init sequence: extended config, NVM complete, wait
// for init-complete, fetch MAC from NVM. Only valid once iwx200_online.
// Returns 0 on success, negative errno otherwise. Bounded, never blocks.
i32 iwx200_run_init(void);
// Last MAC fetched from NVM (valid after a successful run_init).
void iwx200_nvm_mac(u8 out[6]);
// WiFi link state for the network layer.
typedef enum { IWX_LINK_DOWN = 0, IWX_LINK_ASSOCIATING, IWX_LINK_HANDSHAKE, IWX_LINK_UP } iwx_link_t;
iwx_link_t iwx200_link(void);
// Connect to an open or WPA2-PSK network. Runs the firmware MLME sequence
// (scan, auth, assoc, station, keys) plus the software handshake for PSK.
// The passphrase may be NULL for open networks. Bounded, never blocks.
// Returns 0 when the link is up (or already up on the same network).
i32 iwx200_connect(const void* ssid, u32 ssid_length, const char* passphrase);
// Transmit one Ethernet frame over the wireless link (802.11 data + CCMP
// when associated with keys). Returns true when the frame was queued.
bool iwx200_send(const void* frame, u32 length);
// Receive one Ethernet frame from the wireless link (CCMP verified and
// stripped). Returns the length, 0 when no frame is waiting.
u32 iwx200_receive(void* frame, u32 capacity);
// Explicit BSSID for association (from a scan; the UMAC scan command
// arrives with hardware verification). Returns false on a bad address.
bool iwx200_set_bssid(const u8 bssid[6]);
// The INIT image is small; bound every section table.
#define IWX_UCODE_SECT_MAX 69u
#define IWX_FW_MAX_SIZE (4u * 1024u * 1024u)
// Firmware TLV container (little-endian on the wire).
#define IWX_TLV_MAGIC 0x0a4c5749u
#define IWX_TLV_SEC_INIT 20u
#define IWX_TLV_SEC_RT 19u
#define IWX_TLV_N_SCAN_CHANNELS 31u
#define IWX_TLV_PAGING 32u
#define IWX_TLV_FW_VERSION 36u
#define IWX_TLV_CMD_VERSIONS 48u
#define IWX_SEP_CPU12 0xFFFFCCCCu
#define IWX_SEP_PAGING 0xAAAABBBBu
// Alive notification handshake.
#define IWX_NOTIF_ALIVE 0x1u
#define IWX_ALIVE_OK 0xCAFEu
#define IWX_ALIVE_ERR 0xDEADu
// init completion: polled UC_GP1 bit once alive.
#define IWX_GP1_INIT_COMPLETE (1u << 0)
// Driver interface: probe the PCI bus, run the card to the alive state, and
// report the MAC address. Every step is bounded; false means the card is
// absent or unusable, never a hang. RX/TX queue operation arrives later.
bool iwx200_init(void);
bool iwx200_online(void);
void iwx200_mac(u8 out[6]);
// Firmware image staged by the loader: parsed TLV sections in memory.
// The first load uses REGULAR sections; the INIT image runs later.
typedef struct {
    u64 address;
    u32 device_offset;
    u32 length;
} iwx200_sect_t;
typedef struct {
    iwx200_sect_t init[IWX_UCODE_SECT_MAX];
    u32 init_count;
    iwx200_sect_t regular[IWX_UCODE_SECT_MAX];
    u32 regular_count;
    u32 api;
    char version[64];
} iwx200_fw_t;
// Parse a .ucode image already in memory. The image stays owned by the
// caller; only INIT sections are recorded. False on any malformed input.
bool iwx200_parse_fw(const void* image, u64 size, iwx200_fw_t* out);
// Stage a firmware image for the next init attempt. Copies nothing; the
// caller keeps the image alive until init completes. False when the card
// was already probed or the image is malformed.
bool iwx200_stage_fw(const void* image, u64 size);
// After the root filesystem is mounted: load the firmware blob from
// /lib/firmware, then probe the card. Safe to call when no card exists.
void iwx200_late_init(void);
// Synchronous firmware command: send the payload and copy the response.
// The (group, opcode) address the command; the version selects the layout.
// Returns the response length, or a negative errno on timeout/failure.
// Only valid once iwx200_online; never blocks, bounded by the TSC clock.
i32 iwx200_command(u32 group, u32 opcode, u32 version,
                   const void* payload, u32 payload_length,
                   void* response, u32 capacity);
