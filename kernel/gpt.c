// This file has been generated in whole or in part by a Large Language Model
// GPT LBAs use native device blocks; the rest of Kunix addresses 512-byte sectors.
// Layout: https://uefi.org/specs/UEFI/2.11/05_GUID_Partition_Table_Format.html
#include <gpt.h>
#include <disk.h>
#include <memory.h>
#include <errno.h>

static u32 le32(const u8* p) { return (u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24; }
static u64 le64(const u8* p) { return le32(p)|(u64)le32(p+4)<<32; }
static bool equal(const u8* a,const u8* b,u32 n) {
    for(u32 i=0;i<n;i++) if(a[i]!=b[i]) return false;
    return true;
}
static bool zero(const u8* p,u32 n) {
    for(u32 i=0;i<n;i++) if(p[i]) return false;
    return true;
}
static u32 crc32(const u8* p,u32 n) {
    u32 c=~0U;
    for(u32 i=0;i<n;i++) {
        c^=p[i];
        for(u32 j=0;j<8;j++) c=(c>>1)^((0-(c&1))&0xedb88320U);
    }
    return ~c;
}
static int hex(char c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static const u8 order[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
bool gpt_guid_parse(const char* text,u8 out[16]) {
    u8 result[16];
    if(!text || !out) return false;
    u32 pos=0;
    for(u32 i=0;i<16;i++) {
        if(i==4 || i==6 || i==8 || i==10) { if(text[pos++]!='-') return false; }
        int a=hex(text[pos]); if(a<0) return false; pos++;
        int b=hex(text[pos]); if(b<0) return false; pos++;
        result[order[i]]=(a<<4)|b;
    }
    if(text[pos]) return false;
    for(u32 i=0;i<16;i++) out[i]=result[i];
    return true;
}
void gpt_guid_format(const u8 guid[16],char out[37]) {
    static const char digits[]="0123456789abcdef";
    u32 pos=0;
    for(u32 i=0;i<16;i++) {
        if(i==4 || i==6 || i==8 || i==10) out[pos++]='-';
        u8 b=guid[order[i]];
        out[pos++]=digits[b>>4]; out[pos++]=digits[b&15];
    }
    out[pos]=0;
}
typedef struct {
    u64 first,last;
    u32 slots,stride,bytes,count;
    u8 disk_guid[16];
    u8* entries;
    gpt_partition_t part[GPT_MAX_PARTITIONS];
} table_t;

static i32 table_read(u8 disk,u32 block,u64 blocks,bool backup,table_t* t,u8* header) {
    u64 here=backup ? blocks-1 : 1, peer=backup ? 1 : blocks-1;
    u32 ratio=block/512;
    t->entries=0; t->count=0;
    if(!disk_read(disk,here*ratio,ratio,header)) return -EIO;
    if(!equal(header,(const u8*)"EFI PART",8) || le32(header+8)!=0x10000 || le32(header+20)) return -EINVAL;
    u32 hsize=le32(header+12), expected=le32(header+16);
    if(hsize<92 || hsize>block) return -EINVAL;
    for(u32 i=16;i<20;i++) header[i]=0;
    if(crc32(header,hsize)!=expected) return -EINVAL;
    if(le64(header+24)!=here || le64(header+32)!=peer) return -EINVAL;
    t->first=le64(header+40); t->last=le64(header+48);
    if(t->first<2 || t->first>t->last || t->last>=blocks-1 || zero(header+56,16)) return -EINVAL;
    for(u32 i=0;i<16;i++) t->disk_guid[i]=header[56+i];
    t->slots=le32(header+80); t->stride=le32(header+84);
    if(!t->slots || t->slots>GPT_MAX_PARTITIONS || t->stride<128 || t->stride>4096 || (t->stride&(t->stride-1))) return -EINVAL;
    t->bytes=t->slots*t->stride; // bounded above by 512 KiB
    u64 span=((u64)t->bytes+block-1)/block, at=le64(header+72);
    // Both copies live outside all usable partition data. Subtraction avoids wrap.
    if(at<2 || at>=blocks-1 || span>blocks-1-at) return -EINVAL;
    if(backup ? (at<=t->last) : (at>=t->first || span>t->first-at)) return -EINVAL;
    // Leave room for the other copy as well, even when it cannot be recovered.
    u64 reserved=(16384+block-1)/block;
    if(reserved<span) reserved=span;
    if(t->first<2+reserved || reserved>blocks-1-t->last-1) return -EINVAL;
    u32 sectors=(t->bytes+511)/512;
    t->entries=kmalloc((u64)sectors*512);
    if(!t->entries) return -ENOMEM;
    if(!disk_read(disk,at*ratio,sectors,t->entries)) return -EIO;
    if(crc32(t->entries,t->bytes)!=le32(header+88)) return -EINVAL;
    for(u32 i=0;i<t->slots;i++) {
        const u8* p=t->entries+i*t->stride;
        if(zero(p,16)) continue;
        u64 first=le64(p+32),last=le64(p+40);
        if(zero(p+16,16) || first<t->first || first>last || last>t->last) return -EINVAL;
        for(u32 j=0;j<t->count;j++) {
            const gpt_partition_t* other=&t->part[j];
            if(equal(other->guid,p+16,16)) return -EINVAL;
            u64 low=other->start/ratio,high=low+other->count/ratio-1;
            if(first<=high && last>=low) return -EINVAL;
        }
        gpt_partition_t* part=&t->part[t->count++];
        part->parent=disk; part->slot=i+1;
        part->start=first*ratio; part->count=(last-first+1)*ratio;
        for(u32 j=0;j<16;j++) { part->type[j]=p[j]; part->guid[j]=p[16+j]; }
        part->attributes=le64(p+48);
        for(u32 j=0;j<36;j++) {
            u32 c=(u32)p[56+j*2]|(u32)p[57+j*2]<<8;
            part->name[j]=(c>=32 && c<=126) ? c : (c ? '?' : 0);
        }
        part->name[36]=0;
    }
    return 0;
}

i32 gpt_scan(u8 disk,gpt_partition_t* out,u32 cap) {
    u64 sectors=disk_sectors(disk);
    u32 block=disk_block_size(disk);
    if(block!=512 && block!=4096) return -EINVAL;
    u32 ratio=block/512;
    if(sectors%ratio || sectors/ratio<6) return -EINVAL;
    u64 blocks=sectors/ratio;
    u8* header=kmalloc(block);
    table_t* a=kmalloc(sizeof(*a));
    table_t* b=kmalloc(sizeof(*b));
    i32 result=-ENOMEM;
    if(!header || !a || !b) goto done;
    a->entries=b->entries=0;
    if(!disk_read(disk,0,1,header)) { result=-EIO; goto tables_done; }
    result=-EINVAL;
    if(header[510]!=0x55 || header[511]!=0xaa) goto tables_done;
    u32 protective=0;
    for(u32 i=0;i<4;i++) {
        const u8* p=header+446+i*16;
        if(p[4]==0xee) {
            u32 size=blocks-1>0xffffffffULL ? 0xffffffffU : (u32)(blocks-1);
            if(p[0] || le32(p+8)!=1 || le32(p+12)!=size) goto tables_done;
            protective++;
        } else if(!zero(p,16)) goto tables_done; // refuse ambiguous hybrid MBRs
    }
    if(protective!=1) goto tables_done;
    i32 ra=table_read(disk,block,blocks,false,a,header);
    i32 rb=table_read(disk,block,blocks,true,b,header);
    // An allocation failure must not silently turn validation into recovery.
    if(ra==-ENOMEM || rb==-ENOMEM) { result=-ENOMEM; goto tables_done; }
    if(ra && rb) { result=(ra==-EIO || rb==-EIO) ? -EIO : -EINVAL; goto tables_done; }
    if(!ra && !rb && (a->first!=b->first || a->last!=b->last || a->slots!=b->slots || a->stride!=b->stride || !equal(a->disk_guid,b->disk_guid,16) || !equal(a->entries,b->entries,a->bytes))) goto tables_done;
    table_t* selected=ra ? b : a;
    if(selected->count>cap) { result=-ENOSPC; goto tables_done; }
    if(selected->count && !out) { result=-EINVAL; goto tables_done; }
    for(u32 i=0;i<selected->count;i++) out[i]=selected->part[i];
    result=selected->count;
tables_done:
    kfree(a->entries); kfree(b->entries);
done:
    kfree(header); kfree(a); kfree(b);
    return result;
}
