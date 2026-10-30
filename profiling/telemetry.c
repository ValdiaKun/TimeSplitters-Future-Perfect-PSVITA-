/* SPDX-License-Identifier: GPL-3.0-only */
#include "telemetry.h"
#include <string.h>

static void increment(uint64_t *value) {
    if (*value != UINT64_MAX) ++*value;
}
void tsfp_telemetry_reset(TsfpTelemetry *trace, int enabled) {
    memset(trace, 0, sizeof *trace);
    trace->enabled = !!enabled;
}
int tsfp_telemetry_interval(TsfpTelemetry *trace, TsfpTimeKind kind,
                            uint64_t begin_us, uint64_t end_us) {
    if (!trace->enabled) return 0;
    if ((unsigned)kind >= TSFP_TIME_COUNT || end_us < begin_us) {
        increment(&trace->rejected);
        return 0;
    }
    if (trace->count == TSFP_TRACE_CAPACITY) {
        increment(&trace->dropped);
        return 0;
    }
    trace->samples[trace->count++] = (TsfpTraceSample){begin_us, end_us, kind};
    return 1;
}
void tsfp_telemetry_add(TsfpTelemetry *trace, TsfpCounterKind kind, uint64_t amount) {
    if (!trace->enabled) return;
    if ((unsigned)kind >= TSFP_COUNT_COUNT) {
        increment(&trace->rejected);
        return;
    }
    if (amount > UINT64_MAX - trace->counters[kind]) {
        trace->counters[kind] = UINT64_MAX;
        trace->saturated_counters |= UINT32_C(1) << (unsigned)kind;
    } else trace->counters[kind] += amount;
}
void tsfp_telemetry_memory(TsfpTelemetry *trace, uint64_t observed_bytes) {
    if (trace->enabled && observed_bytes > trace->memory_high_water_bytes)
        trace->memory_high_water_bytes = observed_bytes;
}
static uint64_t percentile(const uint64_t *values, uint32_t count, uint32_t percent) {
    return values[(count * percent + 99u) / 100u - 1u];
}
int tsfp_telemetry_summary(const TsfpTelemetry *trace, TsfpTimeKind kind,
                           TsfpTimeSummary *summary) {
    uint64_t values[TSFP_TRACE_CAPACITY];
    uint32_t count = 0;
    memset(summary, 0, sizeof *summary);
    if ((unsigned)kind >= TSFP_TIME_COUNT) return 0;
    for (uint32_t i = 0; i < trace->count; ++i) {
        if (trace->samples[i].kind != kind) continue;
        uint64_t value = trace->samples[i].end_us - trace->samples[i].begin_us;
        uint32_t j = count++;
        while (j && values[j - 1] > value) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = value;
    }
    if (!count) return 0;
    *summary = (TsfpTimeSummary){count, values[0], percentile(values, count, 50),
        percentile(values, count, 95), percentile(values, count, 99), values[count - 1]};
    return 1;
}
