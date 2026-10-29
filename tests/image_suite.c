/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "image.h"
#include "kernel.h"
#include "memory.h"
#include "string_ops.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void fixture_image_entry(void); void fixture_fault_store(void); void fixture_flag_leaf(void);
extern const unsigned char fixture_string_3_1_1_bytes[];
extern const size_t fixture_string_3_1_1_size;
recomp_func_t string_fixture(unsigned kind,unsigned width,unsigned mode);
extern const unsigned char fixture_title_entry_bytes[],fixture_image_entry_bytes[],fixture_fault_store_bytes[];
extern const size_t fixture_title_entry_size,fixture_image_entry_size,fixture_fault_store_size;
static unsigned char data[0x4100],scratch[0x4100],before[0x4100];
static unsigned char other_before[0x2060],other_after[0x2060];
static void snapshot(unsigned char *out) {
    tsfp_guest_read(0x13300,out,0x40); tsfp_guest_read(0x13400,out+0x40,0x20);
    tsfp_guest_read(0x8d000,out+0x60,0x1000); tsfp_guest_read(0x90000,out+0x1060,0x1000);
}
static TsfpImageFunction lookup(uint32_t address) { return address==0x13400 ? fixture_image_entry : NULL; }
static void write_word(unsigned offset,uint32_t word) { memcpy(data+offset,&word,4); }
static TsfpImageDescription description(TsfpImageSection *s) {
    memset(data,0,sizeof(data));
    write_word(0,0x80000068); write_word(4,0x80000140); write_word(8,0x80000102);
    write_word(0x4004,99); write_word(0x3000+0x28,0x76543210);
    s[0]=(TsfpImageSection){0x13300,0x40,5,fixture_title_entry_bytes,fixture_title_entry_size};
    s[1]=(TsfpImageSection){0x13400,0x20,5,fixture_image_entry_bytes,fixture_image_entry_size};
    s[2]=(TsfpImageSection){0x88000,sizeof(data),3,data,sizeof(data)};
    s[3]=(TsfpImageSection){0x8d000,0x1000,3,NULL,0};
    s[4]=(TsfpImageSection){0x90000,0x1000,3,NULL,0};
    TsfpImageDescription d={0};
    d.sections=s; d.count=5; d.entry=0x13400; d.import_table=0x88000; d.import_count=3;
    d.thread_id=41; d.stack_base=0x8d000; d.stack_size=0x1000;
    d.tib=0x89000; d.tls_template=0x8c000; d.tls_size=16; d.thread_data_size=16;
    d.object=0x8a000; d.object_template=0x8b000; d.resolve=lookup;
    return d;
}
static void *out_of_memory(size_t size) { assert(size<=TSFP_IMAGE_MAX_BYTES); return NULL; }
static void invoke(void *context) { (*(recomp_func_t *)context)(); }
static void readonly_store(void *unused) { (void)unused; tsfp_guest_store_u32(0x04013300,0); }
static void not_executable(void *unused) { (void)unused; tsfp_guest_instruction(0x88000); }
static void tail_execute(void *unused) { (void)unused; tsfp_guest_instruction(0x13410); }
static void unbind_active(void *unused) { (void)unused; tsfp_guest_unbind(); }
static void reset_in_boundary(void *unused) { (void)unused; assert(!tsfp_image_reset()); }
static void outside_read(void *unused) { (void)unused; (void)tsfp_guest_load_u32(0x96000); }
void test_image(void) {
    TsfpImageSection sections[8];
    TsfpImageDescription d=description(sections);
    memset(scratch,0xa5,sizeof(scratch)); tsfp_guest_write(0x88000,scratch,sizeof(scratch));
    memcpy(before,scratch,sizeof(before)); snapshot(other_before);
    tsfp_guest_store_u32(0x95000,0x800001ff); assert(tsfp_kernel_imports_prepare(0x95000,1));
    TsfpCpuState cpu_before,cpu_after; tsfp_cpu_capture(&cpu_before);
    unsigned rejected=0;
    for (unsigned variant=0;variant<26;++variant) {
        d=description(sections);
        switch (variant) {
        case 0: d.count=0; break;
        case 1: d.count=17; break;
        case 2: sections[0].size=0; break;
        case 3: sections[0].file_size=65; break;
        case 4: sections[0].data=NULL; break;
        case 5: sections[0].flags=7; break;
        case 6: sections[0].flags=0x85; break;
        case 7: sections[0].address=0xfffffff0; break;
        case 8: sections[0].address=0xfd000000; break;
        case 9: sections[0].address=0x80000000; break;
        case 10: sections[1].address=sections[0].address; break;
        case 11: sections[1].address=sections[0].address+0x04000000; break;
        case 12: d.entry=0x88000; break;
        case 13: d.entry=0x13410; break; /* zero-filled executable tail */
        case 14: d.entry=0x13300; break; /* executable, missing compiled entry */
        case 15: d.import_count=SIZE_MAX; break;
        case 16: write_word(0,0); break;
        case 17: write_word(0,0x80000200); break;
        case 18: d.stack_base=d.tib; break;
        case 19: d.object=d.tib+0x04000000; break;
        case 20: d.tls_template=d.tib; break;
        case 21: d.tls_size=UINT32_MAX; break;
        case 22: sections[3].size=TSFP_IMAGE_MAX_BYTES; break;
        case 23: d.thread_id=0; break;
        case 24: d.object++; break;
        case 25: d.import_table=0x13300; break;
        }
        assert(tsfp_image_initialize(&d)!=TSFP_IMAGE_OK && !tsfp_image_active());
        tsfp_guest_read(0x88000,scratch,sizeof(scratch)); assert(!memcmp(before,scratch,sizeof(before)));
        snapshot(other_after); assert(!memcmp(other_before,other_after,sizeof(other_before)));
        tsfp_cpu_capture(&cpu_after); assert(!memcmp(&cpu_before,&cpu_after,sizeof(cpu_before)));
        recomp_func_t old=tsfp_kernel_lookup(0xfe000000); TsfpFault f;
        assert(old && tsfp_guest_run(invoke,&old,&f)==1 && f.kind==TSFP_FAULT_IMPORT && f.detail==511);
        ++rejected;
    }
    d=description(sections);
    assert(tsfp_image_set_allocator(out_of_memory,free));
    assert(tsfp_image_initialize(&d)==TSFP_IMAGE_ALLOCATION && !tsfp_image_active());
    snapshot(other_after); assert(!memcmp(other_before,other_after,sizeof(other_before)));
    tsfp_guest_read(0x88000,scratch,sizeof(scratch)); assert(!memcmp(before,scratch,sizeof(before)));
    assert(tsfp_image_set_allocator(NULL,NULL)); ++rejected;
    /* A bounded section may cross a low-RAM mirror edge; BSS wraps correctly. */
    static const unsigned char edge[]={1,2,3,4};
    sections[5]=(TsfpImageSection){0x03fffffe,8,3,edge,4}; d.count=8;
    sections[7]=(TsfpImageSection){0x12660,3,5,fixture_string_3_1_1_bytes,fixture_string_3_1_1_size};
    sections[6]=(TsfpImageSection){0x13280,3,5,fixture_fault_store_bytes,fixture_fault_store_size};
    assert(tsfp_image_initialize(&d)==TSFP_IMAGE_OK && tsfp_image_active());
    assert(tsfp_guest_load_u32(0x03fffffe)==0x04030201 && tsfp_guest_load_u32(2)==0);
    assert(tsfp_guest_load_u32(0x8a028)==0x76543210 && tsfp_guest_load_u32(0x8a12c)==41);
    assert(tsfp_guest_load_u32(0x89028)==0x8a000 && tsfp_guest_load_u32(0x89004)==0x89050);
    assert(tsfp_guest_load_u32(0x88000)==0xfe000000 && tsfp_guest_load_u32(0x90000)==0);
    TsfpFault f;
    assert(tsfp_image_initialize(&d)==TSFP_IMAGE_BUSY);
    assert(tsfp_guest_run(readonly_store,NULL,&f)==1 && f.kind==TSFP_FAULT_MEMORY && f.access==TSFP_ACCESS_WRITE);
    assert(tsfp_guest_load_u8(0x13300)==fixture_title_entry_bytes[0]);
    assert(tsfp_guest_run(not_executable,NULL,&f)==1 && f.access==TSFP_ACCESS_EXECUTE);
    assert(tsfp_guest_run(tail_execute,NULL,&f)==1 && f.access==TSFP_ACCESS_EXECUTE);
    assert(tsfp_guest_run(unbind_active,NULL,&f)==1 && f.kind==TSFP_FAULT_MEMORY && tsfp_image_active());
    assert(tsfp_guest_run(reset_in_boundary,NULL,&f)==0 && tsfp_image_active());
    assert(tsfp_guest_run(outside_read,NULL,&f)==1 && f.access==TSFP_ACCESS_READ);
    /* Valid physical RAM outside the image faults after two equal comparisons;
     * Intel REP must restore entry flags for software-permission faults too. */
    recomp_func_t compare=string_fixture(3,1,1);
    g_esi=0x04000004; g_edi=0x88020; g_ecx=3; g_df=0; g_tsfp_guest_flags=0x893;
    assert(tsfp_guest_run(invoke,&compare,&f)==1 && f.access==TSFP_ACCESS_READ);
    assert(f.address==0x04000006 && g_ecx==1 && g_esi==0x04000006 && g_edi==0x88022);
    assert((g_tsfp_guest_flags&0xcd5u)==(0x893u&0xcd5u));
    recomp_func_t forbidden=fixture_flag_leaf;
    assert(tsfp_guest_run(invoke,&forbidden,&f)==1 && f.access==TSFP_ACCESS_EXECUTE && f.cpu.pc==0x13220);
#ifdef TSFP_PORTABLE_GUEST_MEMORY
    g_eax=0; g_edx=0x13300; forbidden=fixture_fault_store;
    assert(tsfp_guest_run(invoke,&forbidden,&f)==1 && f.access==TSFP_ACCESS_WRITE && f.cpu.pc==0x13280);
    assert(tsfp_guest_load_u8(0x13300)==fixture_title_entry_bytes[0]);
#endif
    assert(tsfp_image_run(&f)==1 && f.kind==TSFP_THREAD_EXIT && f.detail==37 && f.thread_id==41);
    assert(!tsfp_guest_thread_id() && tsfp_image_run(&f)==-1);
    assert(tsfp_image_reset() && !tsfp_image_active() && !tsfp_kernel_lookup(0xfe000000));
    assert(tsfp_guest_load_u32(0x8a12c)==41); /* reset preserves guest data */
    assert(tsfp_image_run(&f)==-1);
    /* Valid unknown ordinal installs; actual entry must fault explicitly. */
    d=description(sections); write_word(0,0x800001ff);
    assert(tsfp_image_initialize(&d)==TSFP_IMAGE_OK);
    assert(tsfp_image_run(&f)==1 && f.kind==TSFP_FAULT_IMPORT && f.detail==511);
    assert(tsfp_image_reset());
    printf("image-v1: rejected=%u rollback=pass aliases=pass permissions=pass entry=37 unsupported=pass\n",rejected);
}
