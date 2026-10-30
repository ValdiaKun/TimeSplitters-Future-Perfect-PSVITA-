/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "memory.h"
#include "execution.h"
#include "fault_report.h"
#include "guest_run.h"
#include <inttypes.h>
#include "string_ops.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
void fixture_thread_args(void); void fixture_flag_call(void);
void fixture_fault_load(void); void fixture_fault_store(void); void fixture_fault_divide(void);
void fixture_indirect_mem(void);
static void verify_report(const TsfpFault *fault) {
    TsfpFault snapshot;
    memcpy(&snapshot,fault,sizeof snapshot);
    char output[TSFP_FAULT_JSON_CAPACITY], small[TSFP_FAULT_JSON_CAPACITY];
    char expected[96];
    memset(output, 'x', sizeof output);
    size_t count = tsfp_fault_json(fault, output, sizeof output);
    assert(count && count == strlen(output) && output[0] == '{' && output[count-1] == '}');
    snprintf(expected, sizeof expected, "\"pc\":%" PRIu32 ",", fault->cpu.pc);
    assert(strstr(output, expected));
    snprintf(expected, sizeof expected, "\"thread_id\":%" PRIu32 ",", fault->thread_id);
    assert(strstr(output, expected));
    snprintf(expected, sizeof expected, "\"width\":%zu,", fault->width);
    assert(strstr(output, expected));
    memset(small, 'q', sizeof small);
    assert(!tsfp_fault_json(fault, small, count));
    for (size_t i=0;i<sizeof small;++i) assert(small[i]=='q');
    assert(tsfp_fault_json(fault, small, count+1)==count && !strcmp(output,small));
    assert(!tsfp_fault_json(fault, NULL, SIZE_MAX));
    assert(!memcmp(&snapshot,fault,sizeof snapshot));
}
static void test_report_bounds(void) {
    TsfpFault maximum={0};
    maximum.kind=TSFP_FAULT_IMPORT; maximum.access=TSFP_ACCESS_EXECUTE;
    maximum.cpu.pc=maximum.address=maximum.detail=maximum.thread_id=UINT32_MAX;
    maximum.width=SIZE_MAX; maximum.cpu.flags=maximum.cpu.flags_valid=UINT32_MAX;
    for (unsigned i=0;i<8;++i) maximum.cpu.registers[i]=UINT32_MAX;
    verify_report(&maximum);
    char output[TSFP_FAULT_JSON_CAPACITY]; memset(output,'z',sizeof output);
    maximum.kind=0; assert(!tsfp_fault_json(&maximum,output,sizeof output));
    maximum.kind=(enum TsfpFaultKind)99;
    assert(!tsfp_fault_json(&maximum,output,sizeof output));
    maximum.kind=TSFP_FAULT_IMPORT; maximum.access=(enum TsfpAccess)99;
    assert(!tsfp_fault_json(&maximum,output,sizeof output));
    maximum.kind=TSFP_FAULT_IMPORT; maximum.access=(enum TsfpAccess)-1;
    assert(!tsfp_fault_json(&maximum,output,sizeof output));
    assert(!tsfp_fault_json(NULL,output,sizeof output));
    for (size_t i=0;i<sizeof output;++i) assert(output[i]=='z');
}
static void invoke(void *p) { (*(recomp_func_t *)p)(); }
static void checked_read(void *p) { (void)p; (void)tsfp_guest_load_u32(0xfd000000); }
static void nested(void *p) {
    (void)p;
    TsfpFault inner;
    assert(tsfp_guest_run(checked_read,NULL,&inner)==1);
    assert(inner.kind==TSFP_FAULT_MEMORY && inner.access==TSFP_ACCESS_READ);
    tsfp_guest_thread_exit(73);
}
static void normal(void *p) { (void)p; assert(!tsfp_guest_thread_leave()); }
static void *worker(void *p) {
    uintptr_t id=(uintptr_t)p;
    uint32_t base=0x70000+(uint32_t)id*0x1000u, tls=0x78000+(uint32_t)id*0x100u;
    assert(tsfp_guest_tls_prepare(tls,0x79080,16,16));
    assert(tsfp_guest_load_u32(tls)==UINT32_MAX && tsfp_guest_load_u32(tls+4)==tls+80);
    assert(tsfp_guest_load_u32(tls+64)==tls+80 && tsfp_guest_load_u32(tls+68)==0x12345678);
    tsfp_guest_store_u32(tls+80,(uint32_t)id);
    assert(tsfp_guest_thread_enter((uint32_t)id,base,0x1000,tls,96));
    assert(g_eax==0 && g_fp_control_word==0x37f && g_fs_base==tls && g_tsfp_guest_flags==2);
    assert(!tsfp_guest_thread_enter(99,base,0x1000,tls,0x40));
    for (unsigned i=0;i<32;++i) {
        g_eax=(uint32_t)id; g_tsfp_guest_flags=0x893u+(uint32_t)id;
        TsfpFault f;
        g_ecx=0; g_edx=0; recomp_func_t divide=fixture_fault_divide;
        assert(tsfp_guest_run(invoke,&divide,&f)==1);
        assert(f.kind==TSFP_FAULT_DIVIDE && f.cpu.pc==0x132a0);
        verify_report(&f);
        assert(tsfp_guest_load_u32(g_fs_base+80)==id);
        assert(f.thread_id==id && f.cpu.registers[0]==id && f.cpu.fs_base==tls);
        assert(f.cpu.flags==0x893u+id);
    }
    g_esp=base+0x1000;
    PUSH32(g_esp,(uint32_t)id+100); PUSH32(g_esp,(uint32_t)id+10); PUSH32(g_esp,0);
    recomp_func_t fn=fixture_thread_args;
    TsfpFault f;
    assert(tsfp_guest_run(invoke,&fn,&f)==0);
    assert(g_eax==id+10 && g_edx==id+100 && g_esp==base+0x1000);
    assert(tsfp_guest_thread_leave() && !tsfp_guest_thread_leave());
    return NULL;
}
static _Thread_local unsigned timed_runs;
static void counted_normal(void *context) {
    (void)context; ++timed_runs; normal(NULL);
}
typedef struct FakeClock {
    uint64_t begin,end;
    unsigned calls,fail_mask;
    int expect_cleanup;
} FakeClock;
static _Thread_local int timed_cleaned;
static _Thread_local TsfpTelemetry timed_trace;
static int fake_clock(void *context,uint64_t *stamp) {
    FakeClock *clock=context;
    assert(!tsfp_guest_recovery_active() && clock->calls<2);
    unsigned index=clock->calls++;
    if (index && clock->expect_cleanup) assert(timed_cleaned==1);
    *stamp=index ? clock->end : clock->begin;
    return !(clock->fail_mask & (1u<<index));
}
static void timed_cleanup(void *context) {
    assert(context==&timed_cleaned);
    timed_cleaned=1;
}
static void timed_fault(void *context) {
    (void)context;
    assert(tsfp_guest_scope_guard(timed_cleanup,&timed_cleaned));
    fixture_fault_divide();
}
static void nested_profile(void *context) {
    FakeClock *clock=context; TsfpFault untouched;
    memset(&untouched,0x5a,sizeof untouched);
    TsfpFault before; memcpy(&before,&untouched,sizeof before);
    assert(tsfp_guest_run_profiled(counted_normal,NULL,&untouched,&timed_trace,fake_clock,clock)==-1);
    assert(!memcmp(&untouched,&before,sizeof before) && clock->calls==0);
}
static void test_profiled_runs(void) {
    TsfpFault event;
    timed_runs=0;
    tsfp_telemetry_reset(&timed_trace,0);
    FakeClock clock={100,109,0,0,0};
    assert(tsfp_guest_run_profiled(counted_normal,NULL,&event,&timed_trace,NULL,&clock)==0);
    assert(!clock.calls && !timed_trace.count && !event.kind && timed_runs==1);
    tsfp_telemetry_reset(&timed_trace,1);
    assert(tsfp_guest_run_profiled(counted_normal,NULL,&event,&timed_trace,fake_clock,&clock)==0);
    TsfpTimeSummary summary;
    assert(clock.calls==2 && tsfp_telemetry_summary(&timed_trace,TSFP_TIME_GUEST,&summary));
    assert(summary.count==1 && summary.median_us==9 && !event.kind && timed_runs==2);
    g_eax=10;g_ecx=0;g_edx=0;g_esp=0x70ff0;
    timed_cleaned=0;clock=(FakeClock){200,213,0,0,1};
    assert(tsfp_guest_run_profiled(timed_fault,NULL,&event,&timed_trace,fake_clock,&clock)==1);
    assert(clock.calls==2 && timed_cleaned && event.kind==TSFP_FAULT_DIVIDE && event.cpu.pc==0x132a0);
    assert(event.cpu.registers[0]==10 && event.cpu.registers[4]==0x70ff0);
    assert(timed_trace.count==2 && timed_trace.samples[1].end_us-timed_trace.samples[1].begin_us==13);
    verify_report(&event);
    TsfpFault expected_fault; memcpy(&expected_fault,&event,sizeof expected_fault);
    /* A failed clock pair must not skip guest execution or invent a duration. */
    for (unsigned fail=1;fail<=3;++fail) {
        clock=(FakeClock){1,2,0,fail,0};
        assert(tsfp_guest_run_profiled(counted_normal,NULL,&event,&timed_trace,fake_clock,&clock)==0);
        assert(clock.calls==2 && !event.kind && timed_trace.count==2 && timed_runs==2+fail);
    }
    assert(timed_trace.counters[TSFP_COUNT_CLOCK_FAILURE]==3);
    timed_cleaned=0;clock=(FakeClock){1,2,0,3,1};
    assert(tsfp_guest_run_profiled(timed_fault,NULL,&event,&timed_trace,fake_clock,&clock)==1);
    assert(!memcmp(&expected_fault,&event,sizeof event) && timed_cleaned && timed_runs==5);
    assert(clock.calls==2 && timed_trace.counters[TSFP_COUNT_CLOCK_FAILURE]==4 && timed_trace.count==2);
    clock=(FakeClock){10,9,0,0,0};
    assert(tsfp_guest_run_profiled(counted_normal,NULL,&event,&timed_trace,fake_clock,&clock)==0);
    assert(timed_trace.rejected==1 && timed_trace.count==2 && timed_runs==6);
    tsfp_telemetry_reset(&timed_trace,1);
    for (unsigned i=0;i<TSFP_TRACE_CAPACITY;++i)
        assert(tsfp_telemetry_interval(&timed_trace,TSFP_TIME_GUEST,0,i));
    clock=(FakeClock){1,2,0,0,0};
    assert(tsfp_guest_run_profiled(counted_normal,NULL,&event,&timed_trace,fake_clock,&clock)==0);
    assert(timed_trace.count==TSFP_TRACE_CAPACITY && timed_trace.dropped==1 && timed_runs==7);
    tsfp_telemetry_reset(&timed_trace,1);
    clock=(FakeClock){1,2,0,0,0};
    assert(tsfp_guest_run(nested_profile,&clock,&event)==0 && !timed_trace.count);
    memset(&event,0x5a,sizeof event); TsfpFault before; memcpy(&before,&event,sizeof before);
    assert(tsfp_guest_run_profiled(counted_normal,NULL,&event,&timed_trace,NULL,NULL)==-1);
    assert(tsfp_guest_run_profiled(counted_normal,NULL,&event,NULL,fake_clock,&clock)==-1);
    assert(tsfp_guest_run_profiled(NULL,NULL,&event,&timed_trace,fake_clock,&clock)==-1);
    assert(tsfp_guest_run_profiled(counted_normal,NULL,NULL,&timed_trace,fake_clock,&clock)==-1);
    assert(!memcmp(&event,&before,sizeof before) && !clock.calls && !timed_trace.count && timed_runs==7);
}
void test_execution(void) {
    test_report_bounds();
    TsfpCpuState before,after;
    tsfp_cpu_capture(&before);
    assert(!tsfp_guest_thread_enter(0,0x70000,0x1000,0x78000,64));
    assert(!tsfp_guest_thread_enter(1,0x70001,0x1000,0x78000,64));
    assert(!tsfp_guest_thread_enter(1,0x70000,0x1000,0xfd000000,64));
    tsfp_cpu_capture(&after); assert(memcmp(&before,&after,sizeof(before))==0);
    assert(tsfp_guest_thread_enter(7,0x70000,0x1000,0x78000,64));
    test_profiled_runs();
    TsfpFault fault;
    assert(tsfp_guest_run(normal,NULL,&fault)==0 && fault.kind==0);
    assert(tsfp_guest_run(nested,NULL,&fault)==1 && fault.kind==TSFP_THREAD_EXIT && fault.detail==73);
    assert(!tsfp_guest_recovery_active());
    verify_report(&fault);
    for (unsigned overflow=0;overflow<2;++overflow) {
        g_eax=overflow ? 0xffffffffu : 10; g_edx=overflow ? 1 : 0; g_ecx=overflow ? 1 : 0;
        g_esp=0x70ff0; g_tsfp_guest_flags=0x893; g_xmm7.q[1]=UINT64_C(0x7ff8000000001234);
        recomp_func_t fn=fixture_fault_divide;
        assert(tsfp_guest_run(invoke,&fn,&fault)==1);
        assert(fault.kind==TSFP_FAULT_DIVIDE && fault.address==0x132a0 && fault.detail==overflow);
        assert(fault.cpu.pc==0x132a0 && fault.cpu.registers[0]==g_eax && fault.cpu.registers[2]==g_edx);
        assert(fault.cpu.registers[4]==0x70ff0 && fault.cpu.flags==0x893);
        verify_report(&fault);
        if (!overflow) {
            char report[TSFP_FAULT_JSON_CAPACITY];
            assert(tsfp_fault_json(&fault,report,sizeof report));
            printf("fault-report-fixture: %s\n",report);
        }
        uint64_t payload; memcpy(&payload,fault.cpu.xmm[7]+8,8); assert(payload==g_xmm7.q[1]);
    }
#ifdef TSFP_PORTABLE_GUEST_MEMORY
    for (unsigned write=0;write<2;++write) {
        g_eax=0x12345678; g_ecx=g_edx=0xfd000000; g_esp=0x70ff0;
        recomp_func_t fn=write ? fixture_fault_store : fixture_fault_load;
        assert(tsfp_guest_run(invoke,&fn,&fault)==1);
        assert(fault.kind==TSFP_FAULT_MEMORY && fault.width==4 && fault.address==0xfd000000);
        assert(fault.access==(write ? TSFP_ACCESS_WRITE : TSFP_ACCESS_READ));
        assert(fault.cpu.pc==(write ? 0x13280u : 0x13260u));
        assert(fault.cpu.registers[0]==0x12345678 && fault.cpu.registers[4]==0x70ff0);
        verify_report(&fault);
    }
#endif
    for (unsigned invalid=0;invalid<2;++invalid) {
        uint32_t target=invalid ? 0xfd000000 : 0x10f00;
        tsfp_guest_store_u32(0x79000,target); g_ecx=0x79000; g_esp=0x70ff0; g_eax=77;
        recomp_func_t fn=fixture_indirect_mem;
        assert(tsfp_guest_run(invoke,&fn,&fault)==1);
        assert(fault.kind==TSFP_FAULT_DISPATCH && fault.address==target && fault.detail==(invalid ? 2u : 1u));
        assert(fault.cpu.registers[0]==77 && fault.cpu.registers[4]==0x70fec);
        verify_report(&fault);
    }
    /* Actual translated compare -> direct call to flag-preserving MOV -> RET. */
    tsfp_guest_store_u8(0x79000,0x80); tsfp_guest_store_u8(0x79001,1);
    g_esi=0x79000; g_edi=0x79001; g_df=0; g_esp=0x70ff0;
    PUSH32(g_esp,0); recomp_func_t fn=fixture_flag_call;
    assert(tsfp_guest_run(invoke,&fn,&fault)==0);
    assert(g_ebx==0x12345678 && (g_tsfp_guest_flags&0x8d5u)==0x810u && g_esp==0x70ff0);
    assert(tsfp_guest_thread_leave());
    g_eax=0xabcdef01; g_tsfp_guest_flags=0xc93;
    tsfp_guest_store_u32(0x79084,0x12345678);
    /* Rejected template/layout inputs preserve destination bytes. */
    tsfp_guest_store_u32(0x78000,0xabcdef01);
    assert(!tsfp_guest_tls_prepare(0x78000,0xfd000000,16,16));
    assert(!tsfp_guest_tls_prepare(0x78000,0x79080,SIZE_MAX,16));
    assert(tsfp_guest_load_u32(0x78000)==0xabcdef01);
    pthread_t first,second;
    assert(pthread_create(&first,NULL,worker,(void *)(uintptr_t)1)==0);
    assert(pthread_create(&second,NULL,worker,(void *)(uintptr_t)2)==0);
    assert(pthread_join(first,NULL)==0 && pthread_join(second,NULL)==0);
    assert(g_eax==0xabcdef01 && g_tsfp_guest_flags==0xc93 && !tsfp_guest_thread_id());
    puts("execution-v1: outer-run-timing=pass structured-report=pass capture=pass divide=pass dispatch=pass nested=pass threads=2 tls=pass call-flags=pass");
}
