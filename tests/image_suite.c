/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "image.h"
#include "xbe.h"
#include "kernel.h"
#include "memory.h"
#include "string_ops.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
void fixture_tls_entry(void); void fixture_tls_worker(void);
void fixture_image_entry(void); void fixture_fault_store(void); void fixture_flag_leaf(void);
extern const unsigned char fixture_tls_entry_bytes[],fixture_tls_worker_bytes[];
extern const size_t fixture_tls_entry_size,fixture_tls_worker_size;
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
static TsfpImageFunction lookup(uint32_t address) { return address==0x13400 ? fixture_image_entry : address==0x13500 ? fixture_tls_entry : NULL; }
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
static _Alignas(TsfpXbeImage) unsigned char xbe[0x8000];
static unsigned char xbe_backup[sizeof(xbe)];
static void readonly_import(void *unused) { (void)unused; tsfp_guest_store_u32(0x88000,0); }
static void put(unsigned offset,uint32_t value) { memcpy(xbe+offset,&value,4); }
static void invoke(void *context);
static void *tls_worker(void *context) {
    uintptr_t slot=(uintptr_t)context;
    uint32_t tib=0x89100+(uint32_t)slot*0x100,object=0x8a200+(uint32_t)slot*0x200;
    uint32_t stack=0x91000+(uint32_t)slot*0x1000,id=101+(uint32_t)slot;
    assert(tsfp_guest_thread_enter(id,stack,0x1000,tib,164));
    PUSH32(g_esp,0); recomp_func_t fn=fixture_tls_worker; TsfpFault f;
    assert(tsfp_guest_run(invoke,&fn,&f)==0 && g_eax==99 && g_esp==stack+0x1000);
    assert(g_esi==tib+0x40 && g_edi==tib+0x40);
    assert(tsfp_guest_load_u32(tib+0x48)==id && tsfp_guest_load_u32(object+0x28)==tib+0x40);
    for (unsigned i=20;i<36;++i) assert(tsfp_guest_load_u8(tib+0x40+i)==0);
    assert(tsfp_guest_thread_leave()); return NULL;
}
static unsigned char tls_before[65536],tls_after[65536];
static size_t tls_snapshot(const TsfpXbeImage *x,unsigned char *out) {
    size_t total=0;
    for (size_t i=0;i<x->count;++i) {
        assert(x->sections[i].size<=65536-total);
        tsfp_guest_read(x->sections[i].address,out+total,x->sections[i].size); total+=x->sections[i].size;
    }
    return total;
}
static void tls_negative_cases(size_t size,uint32_t directory_raw,const TsfpXbeImage *valid,TsfpImageDescription placement) {
    unsigned char *saved=xbe_backup; memcpy(saved,xbe,sizeof(xbe));
    size_t total=tls_snapshot(valid,tls_before);
    tsfp_guest_store_u32(0x95000,0x800001ff); assert(tsfp_kernel_imports_prepare(0x95000,1));
    TsfpCpuState before_cpu,after_cpu; tsfp_cpu_capture(&before_cpu);
    for (unsigned variant=0;variant<19;++variant) {
        memcpy(xbe,saved,sizeof(xbe));
        switch (variant) {
        case 0: put(directory_raw+4,0x8c07f); break;
        case 1: put(directory_raw,0xfd000000); put(directory_raw+4,0xfd000014); break;
        case 2: put(directory_raw+4,UINT32_MAX); break;
        case 3: put(directory_raw+16,UINT32_MAX); break;
        case 4: put(directory_raw+8,0xfd000000); break;
        case 5: put(directory_raw+8,0x88000); break;
        case 6: put(directory_raw+8,0x8c021); break;
        case 7: put(directory_raw+8,0x89000); break; /* valid XBE, reserved TIB */
        case 8: put(directory_raw+8,0x8c084); break;
        case 9: put(directory_raw+8,0x8c040); break;
        case 10: put(directory_raw+8,0x8c060); break;
        case 11: put(directory_raw+12,0xfd000000); break;
        case 12: put(directory_raw+32,0x13300); break; /* valid but unsupported callback */
        case 13: put(directory_raw+32,0xfd000000); break;
        case 14: put(directory_raw+20,1); break;
        case 15: put(0x200+2*56+16,0x4044); break;
        case 16: put(directory_raw,0x8c0f8); put(directory_raw+4,0x8c10c); break;
        case 17: put(directory_raw+8,0x04089000); break;
        case 18: put(directory_raw,0x89000); put(directory_raw+4,0x89014); break;
        }
#ifdef TSFP_PC_MAPPED_ORACLE
        if (variant==0 || variant==15) {
            FILE *bad=fopen(variant==0 ? "build/guest/synthetic-tls-reversed.xbe" : "build/guest/synthetic-tls-truncated.xbe","wb");
            assert(bad && fwrite(xbe,1,size,bad)==size && fclose(bad)==0);
        }
#endif
        TsfpXbeImage parsed; memset(&parsed,0xa5,sizeof(parsed));
        unsigned char previous[sizeof(parsed)]; memcpy(previous,&parsed,sizeof(parsed));
        enum TsfpTlsError error=TSFP_TLS_OK;
        int normalized=tsfp_xbe_normalize_ex(xbe,size,&parsed,&error);
        if (variant==7 || variant==18) {
            assert(normalized);
            placement.sections=parsed.sections; placement.count=parsed.count; placement.tls=parsed.tls;
            assert(tsfp_image_initialize(&placement)!=TSFP_IMAGE_OK);
        } else {
            assert(!normalized && error!=TSFP_TLS_OK && !memcmp(previous,&parsed,sizeof(parsed)));
            if (variant==12 || variant==13) assert(error==TSFP_TLS_CALLBACK_UNSUPPORTED);
        }
        assert(!tsfp_image_active());
        recomp_func_t old=tsfp_kernel_lookup(0xfe000000); TsfpFault old_fault;
        assert(old && tsfp_guest_run(invoke,&old,&old_fault)==1 && old_fault.kind==TSFP_FAULT_IMPORT && old_fault.detail==511);
        assert(!tsfp_kernel_lookup(0xfe000004));
        assert(tls_snapshot(valid,tls_after)==total && !memcmp(tls_before,tls_after,total));
        tsfp_cpu_capture(&after_cpu); assert(!memcmp(&before_cpu,&after_cpu,sizeof(before_cpu)));
    }
    memcpy(xbe,saved,sizeof(xbe));
}
static void test_xbe(void) {
    TsfpImageSection sections[8]; TsfpImageDescription d=description(sections);
    write_word(0x4020,0x12345678); /* title-owned TLS index until guest entry */
    write_word(0x4040,0x8c080); write_word(0x4044,0x8c094); write_word(0x4048,0x8c020);
    write_word(0x404c,0x8c060); write_word(0x4050,5); write_word(0x4054,0);
    write_word(0x4080,0x11111111); write_word(0x4084,99); write_word(0x4088,0xdeadbeef);
    write_word(0x408c,0xabcdef01); write_word(0x4090,0x77777777);
    sections[4].size=0x4000;
    sections[6]=(TsfpImageSection){0x13500,0x100,5,fixture_tls_entry_bytes,fixture_tls_entry_size};
    sections[7]=(TsfpImageSection){0x13600,0x40,5,fixture_tls_worker_bytes,fixture_tls_worker_size};
    d.thread_data_size=64;
    sections[5]=(TsfpImageSection){0x88000,16,1,data,16};
    sections[2]=(TsfpImageSection){0x88010,sizeof(data)-16,3,data+16,sizeof(data)-16};
    for (unsigned debug=0;debug<2;++debug) {
        memset(xbe,0,sizeof(xbe)); memcpy(xbe,"XBEH",4);
        put(0x104,0x10000); put(0x108,0x1000); put(0x10c,0x84000); put(0x110,0x178);
        put(0x118,0x10500); put(0x500,0x1d0); /* Authored zero-content certificate. */
        put(0x11c,8); put(0x120,0x10200); put(0x130,0x1000);
        put(0x12c,0x8c040);
        put(0x128,0x13500^(debug ? 0x94859d4bu : 0xa8fc57abu));
        put(0x158,0x88000^(debug ? 0xefb1f152u : 0x5b6d40b6u));
        size_t cursor=0x1000;
        for (unsigned i=0;i<8;++i) {
            unsigned offset=0x200+i*56;
            put(offset,((sections[i].flags&2u) ? 1u : 0u)|(sections[i].flags&4u));
            put(offset+4,sections[i].address); put(offset+8,sections[i].size);
            size_t raw_size=i==0 ? 68 : sections[i].file_size; /* padded raw text */
            put(offset+12,(uint32_t)cursor); put(offset+16,(uint32_t)raw_size);
            if (sections[i].file_size) memcpy(xbe+cursor,sections[i].data,sections[i].file_size);
            cursor+=raw_size;
        }
#ifdef TSFP_PC_MAPPED_ORACLE
        FILE *fixture=fopen(debug ? "build/guest/synthetic-debug.xbe" : "build/guest/synthetic-retail.xbe","wb");
        assert(fixture && fwrite(xbe,1,cursor,fixture)==cursor && fclose(fixture)==0);
#endif
        uint32_t import_raw; memcpy(&import_raw,xbe+0x200+5*56+12,4);
        TsfpXbeImage parsed;
        assert(tsfp_xbe_normalize(xbe,cursor,&parsed) && parsed.entry==0x13500 && parsed.import_count==3 && parsed.sections[0].file_size==64);
        unsigned char parsed_before[sizeof(parsed)]; memcpy(parsed_before,&parsed,sizeof(parsed));
        for (unsigned variant=0;variant<14;++variant) {
            unsigned char *saved=xbe_backup; memcpy(saved,xbe,sizeof(xbe));
            switch (variant) {
            case 0: xbe[0]=0; break;
            case 1: put(0x11c,UINT32_MAX); break;
            case 2: put(0x120,0xffffffff); break;
            case 3: put(0x20c,UINT32_MAX); break;
            case 4: put(0x210,UINT32_MAX); break;
            case 5: put(0x200+56+4,0x13300); break;
            case 6: put(0x128,0); break;
            case 7: put(0x130,0); break;
            case 8: put(import_raw,0x80000200); break;
            case 9: put(import_raw,0x80000000); break;
            case 10: put(0x12c,UINT32_MAX); break;
            case 11: put(0x200+56+12,0x1000); break;
            case 12: put(0x200+5*56+16,12); break; /* terminator only in BSS */
            case 13: put(0x12c,0x90000); break; /* directory only in BSS */
            }
#ifdef TSFP_PC_MAPPED_ORACLE
            if (variant==12 || variant==13) {
                FILE *bad=fopen(variant==12 ? "build/guest/synthetic-bss-import.xbe" : "build/guest/synthetic-bss-tls.xbe","wb");
                assert(bad && fwrite(xbe,1,cursor,bad)==cursor && fclose(bad)==0);
            }
#endif
            assert(!tsfp_xbe_normalize(xbe,cursor,&parsed) && !memcmp(parsed_before,&parsed,sizeof(parsed)));
            memcpy(xbe,saved,sizeof(xbe));
        }
        assert(!tsfp_xbe_normalize(xbe,0x100,&parsed));
        assert(!tsfp_xbe_normalize(xbe,cursor,(TsfpXbeImage *)xbe));
        d.sections=parsed.sections; d.count=parsed.count; d.entry=parsed.entry;
        d.import_table=parsed.import_table; d.import_count=parsed.import_count; d.stack_size=parsed.stack_commit; d.tls=parsed.tls;
        uint32_t data_raw; memcpy(&data_raw,xbe+0x200+2*56+12,4);
        tls_negative_cases(cursor,data_raw+(0x8c040-0x88010),&parsed,d);
        assert(tsfp_image_initialize(&d)==TSFP_IMAGE_OK);
        TsfpCpuState enter_before,enter_after; tsfp_cpu_capture(&enter_before);
        assert(!tsfp_guest_thread_enter(999,0x13300,16,0x89100,164));
        assert(!tsfp_guest_thread_enter(999,0x91000,0x1000,0x13300,16));
        assert(!tsfp_guest_thread_enter(999,0x04089100,0x1000,0x89100,164));
        tsfp_cpu_capture(&enter_after); assert(!memcmp(&enter_before,&enter_after,sizeof(enter_before)));
        for (unsigned slot=0;slot<2;++slot) {
            uint32_t tib=0x89100+slot*0x100,object=0x8a200+slot*0x200;
            assert(tsfp_guest_tls_prepare_xbe(tib,parsed.tls.raw_start,parsed.tls.raw_size,parsed.tls.zero_fill,64));
            assert(tsfp_thread_object_prepare_tls(object,0x8b000,tib,101+slot,tib+0x40));
        }
        tsfp_guest_store_u32(0x89300,0xcafebabe);
        assert(!tsfp_guest_tls_prepare_xbe(0x89300,0x8c080,UINT32_MAX,1,64));
        assert(!tsfp_guest_tls_prepare_xbe(0x0408c040,0x8c080,20,5,64));
        assert(!tsfp_guest_tls_prepare_xbe(0x13300,0x8c080,20,5,64));
        assert(tsfp_guest_load_u32(0x89300)==0xcafebabe);
        assert(tsfp_guest_load_u32(0x8c020)==0x12345678);
#ifdef TSFP_PC_MAPPED_ORACLE
        FILE *state=fopen(debug ? "build/guest/tls-debug-state.bin" : "build/guest/tls-retail-state.bin","wb");
        uint32_t header[]={0x89000,parsed.tls.block_size,tsfp_guest_load_u32(0x8c020),0x8a000};
        size_t tls_bytes=64+parsed.tls.block_size+64;
        tsfp_guest_read(0x89000,scratch,tls_bytes); tsfp_guest_read(0x8a000,scratch+tls_bytes,512);
        assert(state && fwrite(header,1,sizeof(header),state)==sizeof(header));
        assert(fwrite(scratch,1,tls_bytes+512,state)==tls_bytes+512 && !fclose(state));
#endif
        assert(tsfp_guest_load_u32(0x89004)==0x89064 && tsfp_guest_load_u32(0x8a028)==0x89040);
        for (unsigned i=20;i<36;++i) assert(tsfp_guest_load_u8(0x89040+i)==0);
        TsfpFault f; assert(tsfp_image_run(&f)==1 && f.kind==TSFP_THREAD_EXIT && f.detail==37);
        assert(tsfp_guest_load_u32(0x8c020)==0xfffffff7 && tsfp_guest_load_u32(0x90020)==99);
        assert(tsfp_guest_load_u32(0x90024)==0 && tsfp_guest_load_u32(0x90028)==0x89040);
        uint32_t main_eax=g_eax;
        pthread_t workers[2];
        for (uintptr_t slot=0;slot<2;++slot) assert(!pthread_create(&workers[slot],NULL,tls_worker,(void *)slot));
        for (unsigned slot=0;slot<2;++slot) assert(!pthread_join(workers[slot],NULL));
        assert(g_eax==main_eax && tsfp_guest_load_u32(0x89048)==0xdeadbeef);
        assert(tsfp_guest_load_u32(0x8c088)==0xdeadbeef && tsfp_guest_load_u32(0x8c020)==0xfffffff7);
        assert(tsfp_guest_run(readonly_import,NULL,&f)==1 && f.access==TSFP_ACCESS_WRITE);
        assert(tsfp_guest_load_u32(0x88000)==0xfe000000);
        assert(tsfp_image_reset());
        /* Explicit loader-owned index policy: default remains title-owned. */
        d.tls_index_policy=TSFP_TLS_WRITE_INDEX;
        assert(tsfp_image_initialize(&d)==TSFP_IMAGE_OK && tsfp_guest_load_u32(0x8c020)==0xfffffff7);
        assert(tsfp_image_run(&f)==1 && f.kind==TSFP_THREAD_EXIT && f.detail==37);
        assert(tsfp_image_reset()); d.tls_index_policy=TSFP_TLS_TITLE_INDEX;
        /* Empty raw ranges and null callback pointers are valid. */
        uint32_t directory_raw=data_raw+(0x8c040-0x88010);
        unsigned char *saved=xbe_backup; memcpy(saved,xbe,sizeof(xbe));
        for (unsigned zero=0;zero<2;++zero) {
            put(directory_raw,0); put(directory_raw+4,0); put(directory_raw+12,0); put(directory_raw+16,zero ? 5 : 0);
            assert(tsfp_xbe_normalize(xbe,cursor,&parsed) && parsed.tls.raw_size==0);
            assert(parsed.tls.block_size==(zero ? 20u : 4u));
            d.sections=parsed.sections; d.tls=parsed.tls;
            assert(tsfp_image_initialize(&d)==TSFP_IMAGE_OK);
            assert(tsfp_guest_load_u32(0x89004)==0x89040+parsed.tls.block_size);
            assert(tsfp_image_run(&f)==1 && f.kind==TSFP_THREAD_EXIT && f.detail==37);
            assert(tsfp_guest_load_u32(0x8c020)==parsed.tls.index_value && tsfp_guest_load_u32(0x90020)==0);
            assert(tsfp_image_reset());
        }
        memcpy(xbe,saved,sizeof(xbe));
    }
    puts("tls-v1: malformed=38 rollback=pass index=title,loader zero-fill=pass workers=2 callbacks=reject");
    puts("xbe-v1: keys=retail,debug malformed=28 rollback=pass entry=37");
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
    test_xbe();
    printf("image-v1: rejected=%u rollback=pass aliases=pass permissions=pass entry=37 unsupported=pass\n",rejected);
}
