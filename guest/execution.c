/* SPDX-License-Identifier: GPL-3.0-only */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "execution.h"
#include "memory.h"
#include "string_ops.h"
#include "tls.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
_Thread_local uint32_t g_tsfp_guest_pc;
static _Thread_local uint32_t thread_id;
static _Thread_local int attached;
typedef struct Boundary {
    jmp_buf jump;
    struct Boundary *previous;
    TsfpFault *output;
} Boundary;
static _Thread_local Boundary *boundary;
void tsfp_guest_instruction(uint32_t address) {
    g_tsfp_guest_pc=address;
    g_tsfp_access=TSFP_ACCESS_EXECUTE;
    tsfp_guest_check_access(address,1,4);
}
void tsfp_cpu_capture(TsfpCpuState *s) {
    memset(s,0,sizeof(*s));
    uint32_t r[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi};
    memcpy(s->registers,r,sizeof(r));
    s->pc=g_tsfp_guest_pc; s->flags=(g_tsfp_guest_flags&~0x400u)|(g_df ? 0x400u : 0);
    s->flags_valid=g_tsfp_flags_valid|0x400u;
    s->fs_base=g_fs_base; s->seh_ebp=g_seh_ebp;
    RecompXmm *x[]={&g_xmm0,&g_xmm1,&g_xmm2,&g_xmm3,&g_xmm4,&g_xmm5,&g_xmm6,&g_xmm7};
    for (unsigned i=0;i<8;++i) memcpy(s->xmm[i],x[i],16);
    RecompMmx *m[]={&g_mm0,&g_mm1,&g_mm2,&g_mm3,&g_mm4,&g_mm5,&g_mm6,&g_mm7};
    for (unsigned i=0;i<8;++i) memcpy(s->mmx[i],m[i],8);
    memcpy(s->fp,g_fp_stack,sizeof(s->fp));
    s->fp_top=g_fp_top; s->fp_cmp=g_fp_cmp;
    s->fp_control=g_fp_control_word; s->fp_cc=g_fp_cc;
}
int tsfp_guest_run(void (*callback)(void *), void *context, TsfpFault *fault) {
    if (!callback || !fault) abort();
    Boundary scope;
    scope.previous=boundary; scope.output=fault;
    memset(fault,0,sizeof(*fault));
    boundary=&scope;
    if (setjmp(scope.jump)) {
        boundary=scope.previous;
        return 1;
    }
    callback(context);
    boundary=scope.previous;
    return 0;
}
int tsfp_guest_recovery_active(void) { return boundary!=NULL; }
_Noreturn void tsfp_guest_raise(enum TsfpFaultKind kind, uint32_t address, size_t width, uint32_t detail) {
    if (!boundary) abort(); /* No implicit skip or continuation. */
    TsfpFault *f=boundary->output;
    f->kind=kind; f->address=address; f->width=width; f->detail=detail;
    f->thread_id=thread_id; f->access=kind==TSFP_FAULT_MEMORY ? g_tsfp_access : TSFP_ACCESS_UNKNOWN;
    tsfp_cpu_capture(&f->cpu);
    longjmp(boundary->jump,1);
}
uint32_t tsfp_guest_thread_id(void) { return thread_id; }
int tsfp_guest_thread_enter(uint32_t id, uint32_t base, size_t size, uint32_t fs, size_t tls_size) {
    if (attached || boundary || !id || size<16 || (base&3u) || (size&3u) ||
        (uint64_t)base+size>UINT32_MAX || !tsfp_guest_range_valid(base,size) ||
        !tls_size || !tsfp_guest_range_valid(fs,tls_size) ||
        !tsfp_guest_access_allowed(base,size,2) || !tsfp_guest_access_allowed(fs,tls_size,3) ||
        (tsfp_guest_policy_active() && tsfp_tls_ranges_overlap(base,size,fs,tls_size))) return 0;
    g_eax=g_ecx=g_edx=g_ebx=g_ebp=g_esi=g_edi=0;
    g_esp=base+(uint32_t)size; g_seh_ebp=g_esp; g_fs_base=fs;
    g_df=g_fp_top=g_fp_cmp=0; g_fp_control_word=0x37f; g_fp_cc=0;
    memset(g_fp_stack,0,sizeof(g_fp_stack));
    RecompXmm *x[]={&g_xmm0,&g_xmm1,&g_xmm2,&g_xmm3,&g_xmm4,&g_xmm5,&g_xmm6,&g_xmm7};
    for (unsigned i=0;i<8;++i) memset(x[i],0,16);
    RecompMmx *m[]={&g_mm0,&g_mm1,&g_mm2,&g_mm3,&g_mm4,&g_mm5,&g_mm6,&g_mm7};
    for (unsigned i=0;i<8;++i) memset(m[i],0,8);
    g_icall_saved_esp=g_icall_dispatch_form=0;
    g_tsfp_guest_flags=2; g_tsfp_flags_valid=0xcd5; g_tsfp_guest_pc=0; g_tsfp_access=TSFP_ACCESS_UNKNOWN;
    thread_id=id; attached=1;
    return 1;
}
int tsfp_guest_thread_leave(void) {
    if (!attached || boundary) return 0;
    attached=0; thread_id=0; g_fs_base=0;
    return 1;
}

_Noreturn void tsfp_guest_thread_exit(uint32_t status) {
    tsfp_guest_raise(TSFP_THREAD_EXIT,0,0,status);
}
static int prepare_tls(uint32_t destination,uint32_t source,size_t raw_size,size_t template_size,size_t data_size) {
    const size_t limit=65536;
    if (template_size<4 || template_size>limit-64 || data_size>limit-64-template_size) return 0;
    size_t total=64+template_size+data_size;
    if (!tsfp_guest_range_valid(destination,total) || (raw_size && !tsfp_guest_range_valid(source,raw_size))) return 0;
    if (!tsfp_guest_access_allowed(destination,total,2) || (raw_size && !tsfp_guest_access_allowed(source,raw_size,1))) return 0;
    unsigned char *image=calloc(1,total);
    if (!image) return 0;
    /* All guest spans are prevalidated before resources are acquired; memory
     * bindings must not change. Staging handles physically aliased templates. */
    if (raw_size) tsfp_guest_read(source,image+64,raw_size);
    uint32_t end=destination+64u+(uint32_t)template_size, seh=UINT32_MAX;
    memcpy(image,&seh,4); memcpy(image+4,&end,4); memcpy(image+64,&end,4);
    tsfp_guest_write(destination,image,total);
    free(image);
    return 1;
}

int tsfp_guest_tls_prepare(uint32_t destination,uint32_t source,size_t template_size,size_t data_size) {
    return prepare_tls(destination,source,template_size,template_size,data_size);
}
int tsfp_guest_tls_prepare_xbe(uint32_t destination,uint32_t source,uint32_t raw_size,uint32_t zero_fill,size_t data_size) {
    uint32_t total,index;
    if (!tsfp_tls_layout(raw_size,zero_fill,&total,&index) || data_size>65536u-64u-total ||
        (uint64_t)destination+64u+total+data_size>0x74000000u ||
        (raw_size && ((uint64_t)source+raw_size>0x74000000u ||
         tsfp_tls_ranges_overlap(destination,64u+total+data_size,source,raw_size)))) return 0;
    return prepare_tls(destination,source,raw_size,total,data_size);
}
