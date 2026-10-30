/* SPDX-License-Identifier: GPL-3.0-only */
#include "telemetry.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
static TsfpTelemetry trace, copy;
int main(void) {
    TsfpTimeSummary summary;
    tsfp_telemetry_reset(&trace, 0);
    assert(!tsfp_telemetry_interval(&trace, TSFP_TIME_GUEST, 0, 10));
    tsfp_telemetry_add(&trace, TSFP_COUNT_SIMULATION, UINT64_MAX);
    tsfp_telemetry_memory(&trace, UINT64_MAX);
    assert(!tsfp_telemetry_interval(&trace, TSFP_TIME_COUNT, 9, 0));
    tsfp_telemetry_add(&trace, TSFP_COUNT_COUNT, 1);
    assert(trace.rejected == 0);
    assert(trace.count == 0 && trace.counters[0] == 0 && trace.memory_high_water_bytes == 0);
    assert(!tsfp_telemetry_summary(&trace, TSFP_TIME_GUEST, &summary));
    assert(summary.count == 0 && summary.maximum_us == 0);
    tsfp_telemetry_reset(&trace, 1);
    assert(!tsfp_telemetry_interval(&trace, (TsfpTimeKind)-1, 0, 1));
    assert(!tsfp_telemetry_interval(&trace, TSFP_TIME_GUEST, 10, 9));
    tsfp_telemetry_add(&trace, (TsfpCounterKind)-1, 1);
    assert(trace.rejected == 3 && trace.count == 0);
    assert(tsfp_telemetry_interval(&trace, TSFP_TIME_GUEST, 0, UINT64_MAX));
    assert(tsfp_telemetry_summary(&trace, TSFP_TIME_GUEST, &summary));
    assert(summary.count == 1 && summary.p99_us == UINT64_MAX);
    tsfp_telemetry_reset(&trace, 1);
    /* Independent ordered reference: durations 1..100 in reverse insertion. */
    for (unsigned i = 100; i; --i)
        assert(tsfp_telemetry_interval(&trace, TSFP_TIME_GUEST, 7, 7 + i));
    assert(tsfp_telemetry_interval(&trace, TSFP_TIME_KERNEL, 12, 12));
    memcpy(&copy, &trace, sizeof copy);
    assert(tsfp_telemetry_summary(&trace, TSFP_TIME_GUEST, &summary));
    assert(summary.count == 100 && summary.minimum_us == 1 && summary.median_us == 50);
    assert(summary.p95_us == 95 && summary.p99_us == 99 && summary.maximum_us == 100);
    assert(memcmp(&copy, &trace, sizeof trace) == 0);
    assert(tsfp_telemetry_summary(&trace, TSFP_TIME_KERNEL, &summary));
    assert(summary.count == 1 && summary.p95_us == 0);
    assert(!tsfp_telemetry_summary(&trace, TSFP_TIME_LOAD_STALL, &summary));
    assert(summary.count == 0);
    assert(!tsfp_telemetry_summary(&trace, TSFP_TIME_COUNT, &summary));
    tsfp_telemetry_reset(&trace, 1);
    for (unsigned i = 0; i < TSFP_TRACE_CAPACITY; ++i)
        assert(tsfp_telemetry_interval(&trace, TSFP_TIME_DIAGNOSTIC_LOOP, i, i + 1));
    memcpy(&copy, &trace, sizeof copy);
    for (unsigned i = 0; i < 1000; ++i)
        assert(!tsfp_telemetry_interval(&trace, TSFP_TIME_DIAGNOSTIC_LOOP, 0, 999));
    assert(trace.count == TSFP_TRACE_CAPACITY && trace.dropped == 1000);
    assert(memcmp(trace.samples, copy.samples, sizeof trace.samples) == 0);
    trace.dropped = trace.rejected = UINT64_MAX;
    assert(!tsfp_telemetry_interval(&trace, TSFP_TIME_DIAGNOSTIC_LOOP, 0, 1));
    assert(!tsfp_telemetry_interval(&trace, TSFP_TIME_COUNT, 0, 1));
    assert(trace.dropped == UINT64_MAX && trace.rejected == UINT64_MAX);
    for (unsigned i = 0; i < TSFP_COUNT_COUNT; ++i) {
        tsfp_telemetry_add(&trace, (TsfpCounterKind)i, UINT64_MAX);
        assert(trace.counters[i] == UINT64_MAX && !(trace.saturated_counters & (1u << i)));
        tsfp_telemetry_add(&trace, (TsfpCounterKind)i, 1);
        assert(trace.counters[i] == UINT64_MAX && (trace.saturated_counters & (1u << i)));
    }
    tsfp_telemetry_memory(&trace, 4096);
    tsfp_telemetry_memory(&trace, 12);
    assert(trace.memory_high_water_bytes == 4096);
    tsfp_telemetry_reset(&trace, 1);
    assert(trace.count == 0 && trace.dropped == 0 && trace.saturated_counters == 0);
    assert(trace.memory_high_water_bytes == 0 && trace.counters[0] == 0);
    /* Even median follows nearest rank: lower middle (rank ceil(n/2)). */
    assert(tsfp_telemetry_interval(&trace, TSFP_TIME_GUEST, 0, 3));
    assert(tsfp_telemetry_interval(&trace, TSFP_TIME_GUEST, 0, 9));
    assert(tsfp_telemetry_summary(&trace, TSFP_TIME_GUEST, &summary));
    assert(summary.median_us == 3 && summary.p95_us == 9 && summary.p99_us == 9);
    /* Distinct owner recorders do not share mutable global state. */
    tsfp_telemetry_reset(&copy, 1);
    assert(tsfp_telemetry_interval(&copy, TSFP_TIME_KERNEL, 10, 20));
    assert(trace.count == 2 && copy.count == 1);
    tsfp_telemetry_reset(&trace, 1);
    for (unsigned i = TSFP_TRACE_CAPACITY; i; --i)
        assert(tsfp_telemetry_interval(&trace, TSFP_TIME_GUEST, 0, i));
    assert(tsfp_telemetry_summary(&trace, TSFP_TIME_GUEST, &summary));
    assert(summary.count == 256 && summary.median_us == 128);
    assert(summary.p95_us == 244 && summary.p99_us == 254 && summary.maximum_us == 256);
    puts("telemetry bounded recording, percentiles, rejection and saturation: pass");
    return 0;
}
