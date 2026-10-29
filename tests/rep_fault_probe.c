/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "string_native_oracle.h"
#include <assert.h>
#include <cpuid.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
ptrdiff_t g_xbox_mem_offset;
int main(void) {
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    unsigned char *p=mmap(NULL,page*3,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    assert(p!=MAP_FAILED && (uintptr_t)p+page*3<=UINT32_MAX);
    p[0]=p[1]=9; p[2*page-1]=9;
    assert(mprotect(p+page*2,page,PROT_NONE)==0);
    StringState state={(uint32_t)(uintptr_t)p,(uint32_t)(uintptr_t)(p+2*page-1),2,0,0x893,0,0,0};
    native_string_oracle(3,1,1,&state);
    int amd=native_string_amd();
    assert(state.fault && state.source==(uint32_t)(uintptr_t)(p+1));
    assert(state.destination==(uint32_t)(uintptr_t)(p+page*2) && state.count==1);
    /* CF/PF/AF/ZF/SF/OF/DF are all checked; RF/IF are not guest arithmetic. */
    assert((state.flags&0xcd5u)==(amd ? 0x44u : 0x891u));
    unsigned signature,b,c,d; __cpuid(1,signature,b,c,d);
    printf("native-rep-cpu: signature=%x hypervisor=%u\n",signature,(c>>31)&1u);
    printf("native-rep-fault: vendor=%s flags=%x progress=1 expected=%s\n",amd ? "AuthenticAMD" : "GenuineIntel",state.flags&0xcd5u,amd ? "completed-comparison" : "instruction-entry");
    assert(munmap(p,page*3)==0);
}
