// This file has been generated in whole or in part by a Large Language Model
// Wire fixtures follow Intel gen2 ABI as published in OpenBSD if_iwxreg.h.
// These checks establish ABI and bounded handling, never radio validation.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <types.h>
#define IWX200_TEST_MMIO
static u32 iwx_test_read(u32);
static void iwx_test_write(u32,u32);
#include "../../drivers/iwx200.c"

static u32 registers[0x20000 / 4];
static u64 clock_ns;
static u32 pci_bar_low = 0xf0000004, pci_bar_high;
static u16 pci_command;
static bool present, emit_alive, bad_alive, rfkill, answer_commands = true;
static u32 last_kick, prph_address, prph_write_address;
static u64 mapped_first;
static u8 arena[16 * 1024 * 1024] __attribute__((aligned(4096)));
static u64 arena_used;
static u32 iwx_test_read(u32 off) { return registers[off / 4]; }
static void iwx_test_write(u32 off,u32 value) {
    if (off == IWX_CSR_INT || off == IWX_CSR_FH_INT_STATUS) { registers[off/4] &= ~value; return; }
    registers[off/4] = value;
    if (off == 0x448) { prph_address = value; registers[0x450/4] = 0; }
    if (off == 0x444) prph_write_address = value;
    if (off == 0x44c && prph_write_address == 0x03005c44 && value == 1 && emit_alive)
        registers[IWX_CSR_INT/4] |= 1;
}
void outl(u16 p,u32 v) { assert(p==PCI_CONFIG_ADDRESS); (void)v; }
void outw(u16 p,u16 v) { assert(p==PCI_CONFIG_DATA); pci_command=v; }
pci_device_t pci_scan_for_device(u16 v,u16 d) { return present ? (pci_device_t){.vendor_id=v,.device_id=d} : (pci_device_t){0}; }
u32 pci_read_config(u8 b,u8 d,u8 f,u8 off) { (void)b;(void)d;(void)f;return off==0x10?pci_bar_low:off==0x14?pci_bar_high:0; }
void* pmm_alloc(void) { if(arena_used+4096>sizeof(arena))return NULL;void*p=arena+arena_used;arena_used+=4096;memset(p,0,4096);return p; }
void pmm_free(uint64_t p) { (void)p; }
uint64_t vmm_hhdm(void) { return 0; }
bool vmm_map(uint64_t v,uint64_t p,uint64_t flags) { assert(v>=MMIO_BASE&&v<MMIO_BASE+MMIO_SIZE);assert(flags==0x1b);if(v==MMIO_BASE)mapped_first=p;card.regs=(volatile u8*)registers;return true; }
i32 file_open(const char*p,u32 f,file_t**o) {(void)p;(void)f;(void)o;return -2;}
void file_unref(file_t*f){(void)f;}
i64 file_read(file_t*f,void*b,u64 n){(void)f;(void)b;(void)n;return -5;}
void*kmalloc(uint64_t n){(void)n;return NULL;}
void kfree(void*p){(void)p;}
kernel_logs_t log_messages[4];
void klog(kernel_logs_t*l,const char*f,...){(void)l;(void)f;}
static void put_packet(u8* p,u8 group,u8 opcode,u8 index,u8 queue,const void*data,u32 n) {
    // length is command header + payload, and EXCLUDES its own length word.
    memset(p,0,8+n);cmd_put32(p,4+n);p[4]=opcode;p[5]=group;p[6]=index;p[7]=queue;
    if(n)memcpy(p+8,data,n);
}
u64 time_counter_ns(void) {
    clock_ns += 1000000;
    if(registers[IWX_CSR_GP_CNTRL/4]&IWX_GP_INIT_DONE)
        registers[IWX_CSR_GP_CNTRL/4]|=IWX_GP_MAC_CLOCK_READY;
    if(!rfkill)registers[IWX_CSR_GP_CNTRL/4]|=IWX_GP_HW_RFKILL_SW;
    if(card.rx_started && emit_alive && !card.alive && *card.rx_status==0) {
        u8 alive[144]={0};alive[0]=bad_alive?0xad:0xfe;alive[1]=bad_alive?0xde:0xca;
        put_packet(card.rx_buffers[0],0,1,0,0x80,alive,sizeof(alive));*card.rx_status=1;
    }
    u32 kick=registers[IWX_HBUS_WRPTR/4];
    if ((kick >> 16) != 0) last_kick = kick;
    if(answer_commands&&kick!=last_kick&&card.online) {
        last_kick=kick;u32 slot=(card.tx_next+255)%256;const u8*cmd=card.cmd_buffers[slot];
        u8*buffer=card.rx_buffers[*card.rx_status%512];
        u8 reply[468]={0};u32 n=4;
        if(cmd[1]==1&&cmd[0]==0x1d) { n=8;reply[0]=2; }
        else if(cmd[1]==12&&cmd[0]==2) {
            n=468;reply[7]=1;cmd_put32(reply+12,3);cmd_put32(reply+16,3);cmd_put32(reply+24,110);cmd_put32(reply+28,0x0f);
        }
        else {reply[0]=0x78;reply[1]=0x56;reply[2]=0x34;reply[3]=0x12;}
        if(cmd[1]==12&&cmd[0]==0) {
            // INIT_COMPLETE and command ACK can share one RB, 64-byte aligned.
            put_packet(buffer,0,4,0,0x80,NULL,0);
            put_packet(buffer+64,cmd[1],cmd[0],cmd[2],0,reply,n);
        } else if(cmd[1]==5&&cmd[0]==0) put_packet(buffer,0,2,cmd[2],0,reply,n); // DQA not advertised by cc-a0-71
        else put_packet(buffer,cmd[1],cmd[0],cmd[2],0,reply,n);
        *card.rx_status=(*card.rx_status+1)%512;
    }
    return clock_ns;
}
static u32 build_image(u8* image) {
    memset(image,0,256);cmd_put32(image+4,0x0a4c5749);image[72]=71;
    u32 o=88;
    const u32 offsets[]={0x433000,0xffffcccc,0x80433000};
    for(u32 i=0;i<3;i++){cmd_put32(image+o,19);cmd_put32(image+o+4,8);cmd_put32(image+o+8,offsets[i]);image[o+12]=1;o+=16;}
    cmd_put32(image+o,48);cmd_put32(image+o+4,20);const u8 versions[]={1,0,99,6,2,12,1,4,0x0d,1,15,0,0x1d,1,2,2,0x1c,1,9,7};memcpy(image+o+8,versions,sizeof(versions));o+=28;
    cmd_put32(image+o,23);cmd_put32(image+o+4,4);cmd_put32(image+o+8,0x00330018);return o+12;
}
static void reset(void) {
    memset(&card,0,sizeof(card));memset(registers,0,sizeof(registers));clock_ns=0;arena_used=0;last_kick=0;pci_command=0;
    mapped_first=0;prph_address=0;prph_write_address=0;present=true;emit_alive=true;bad_alive=false;rfkill=false;answer_commands=true;
    pci_bar_low=0xf0004004;pci_bar_high=0;
    registers[(IWX_CSR_MAC_CSR_BASE+8)/4]=0x00112233;registers[(IWX_CSR_MAC_CSR_BASE+12)/4]=0x4455;
}
int main(void) {
    assert(sizeof(ctxt_t)==1792);assert(__builtin_offsetof(ctxt_t,umac_image)==192);
    assert(__builtin_offsetof(tfd_t,tb[0].length)==2);assert(__builtin_offsetof(tfd_t,tb[0].address)==4);
    // Parse the actual packaged firmware, not only the synthetic boundary fixture.
    FILE* file=fopen("vendor/firmware/iwlwifi/iwlwifi-cc-a0-71.ucode","rb");assert(file);
    u8*blob=malloc(2*1024*1024);assert(blob);size_t blob_size=fread(blob,1,2*1024*1024,file);fclose(file);
    iwx200_fw_t packaged;assert(iwx200_parse_fw(blob,blob_size,&packaged));
    assert(blob_size==1322288&&packaged.api==71&&packaged.regular_count==47);
    assert(packaged.alive_version==6&&packaged.nvm_version==4&&packaged.phy_config==0x00330018);
    assert(packaged.capabilities[0]==0x9def037f&&!(packaged.capabilities[0]&(1u<<12)));
    free(blob);
    u8 image[256];u32 n=build_image(image);iwx200_fw_t fw;
    assert(iwx200_parse_fw(image,n,&fw));assert(fw.api==71&&fw.alive_version==6&&fw.nvm_version==4&&fw.regular_count==3);
    assert(!iwx200_parse_fw(image,n-1,&fw));assert(!iwx200_parse_fw(NULL,n,&fw));
    reset();iwx200_late_init();assert(card.status.present&&card.status.stage==IWX200_FIRMWARE_MISSING);
    assert(!iwx200_init());assert(!card.attempted); // early call leaves staging possible
    assert(iwx200_stage_fw(image,n));present=false;assert(!iwx200_init());assert(card.status.stage==IWX200_ABSENT);
    reset();assert(iwx200_stage_fw(image,n));rfkill=true;assert(iwx200_init());assert(card.status.rfkill_clear==false);
    reset();assert(iwx200_stage_fw(image,n));emit_alive=false;registers[IWX_CSR_UCODE_DRV_GP1/4]=1;
    assert(!iwx200_init());assert(!card.alive);assert(card.status.stage==IWX200_ALIVE);assert(!(pci_command&4));
    reset();assert(iwx200_stage_fw(image,n));bad_alive=true;assert(!iwx200_init());assert(!(pci_command&4));
    reset();assert(iwx200_stage_fw(image,n));pci_bar_high=1;
    assert(iwx200_init());assert(mapped_first==0x1f0004000ULL);assert((pci_command&6)==6);
    assert(registers[IWX_CSR_LTR_LONG_VAL/4]==0x82fa82fa);
    assert(prph_address==0x03003440);assert(prph_write_address==0x03005c44);
    const ctxt_t*context=(const ctxt_t*)card.ctxt_phys;assert(context->size==448);assert(context->rx_free!=context->rx_used);assert(context->lmac_image[0]&&context->umac_image[0]);
    assert(registers[IWX_CSR_RX_WPTR/4]==0); // ALIVE's buffer has been consumed
    assert(iwx200_cmd_version(1,13)==15);assert(iwx200_notif_version(0,1)==6);
    u8 payload[1972]={0},response[512];assert(iwx200_command(0,0x0d,0,payload,sizeof(payload),response,sizeof(response))==4);
    const u8*tfd=(const u8*)card.tx_ring;assert(tfd[0]==2&&tfd[2]==20);assert(get32(tfd+4)==(u32)card.cmd_addresses[0]);
    assert(tfd[12]==(u8)(1980-20)&&tfd[13]==(u8)((1980-20)>>8));assert(card.cmd_buffers[0][1]==1);
    assert(response[0]==0x78&&response[3]==0x12); // no 4-byte reply truncation
    assert(iwx200_command(1,0x0c,0,NULL,0,NULL,0)==0); // ACK-only caller discards firmware payload
    assert(iwx200_run_init()==0);iwx200_nvm_t config;assert(iwx200_get_nvm(&config));assert(config.channel_count==110&&config.tx_chains==3&&config.channel_profiles[0]==15);
    assert(card.status.stage==IWX200_READY);
    // closed_rb_num has a12-bit hardware counter; use the configured ring mask.
    u8 marker=0x42;u32 rx_index=card.rx_next;
    put_packet(card.rx_buffers[rx_index],0,0x13,0,0x80,&marker,1);
    *card.rx_status=(u16)(((rx_index+1)%512)|0x200);
    const volatile u8*packet=NULL;assert(rx_poll(&packet)==9&&packet[8]==0x42);
    assert(iwx200_configure_radio()==0);
    i32 qid=iwx200_data_queue_enable(0,15);assert(qid==2);
    u8 txprefix[20]={0},frame[64]={0};frame[0]=0x08;frame[1]=1;cmd_put16(txprefix,sizeof(frame));
    assert(iwx200_data_tx(qid,txprefix,frame,sizeof(frame)));assert(card.data[0].pending==1);
    const u8*data_tfd=card.data[0].ring;assert(data_tfd[0]==3&&data_tfd[2]==20&&data_tfd[12]==28&&data_tfd[22]==40);
    assert(card.data[0].buffers[0][3]==2&&card.data[0].byte_counts[0]==16);
    u8 completed[48]={0};completed[0]=1;cmd_put32(completed+44,1);
    put_packet(card.rx_buffers[*card.rx_status%512],0,0x1c,0,2,completed,sizeof(completed));*card.rx_status=(*card.rx_status+1)%512;
    iwx200_poll();assert(card.data[0].pending==0&&card.data[0].tail==1);
    assert(iwx200_data_queue_disable(qid)==0);
    answer_commands=false;assert(iwx200_command(2,3,0,NULL,0,response,sizeof(response))==-110);assert(!iwx200_online());assert(!(pci_command&4));
    puts("iwx200 gen2 ABI, PCI/DMA, RX ALIVE, wide commands and NVM: PASS (host only)");return 0;
}
