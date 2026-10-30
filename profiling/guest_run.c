/* SPDX-License-Identifier: GPL-3.0-only */
#include "guest_run.h"
int tsfp_guest_run_profiled(void (*callback)(void *),void *context,TsfpFault *fault,
                            TsfpTelemetry *trace,TsfpProfileClock clock,void *clock_context) {
    if (!callback || !fault || !trace || (trace->enabled && !clock) || tsfp_guest_recovery_active())
        return -1;
    uint64_t begin_us=0,end_us=0;
    int enabled=trace->enabled;
    int have_begin=enabled && clock(clock_context,&begin_us);
    int result=tsfp_guest_run(callback,context,fault);
    if (enabled) {
        int have_end=clock(clock_context,&end_us);
        if (have_begin && have_end) tsfp_telemetry_interval(trace,TSFP_TIME_GUEST,begin_us,end_us);
        else tsfp_telemetry_add(trace,TSFP_COUNT_CLOCK_FAILURE,1);
    }
    return result;
}
