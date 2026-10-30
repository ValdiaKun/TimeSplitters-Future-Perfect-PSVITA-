/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_TELEMETRY_H
#define TSFP_TELEMETRY_H
#include <stdint.h>
#define TSFP_TRACE_CAPACITY 256u
/* One recorder per owner thread. No concurrent reads, reset or reporting.
 * Times are caller-supplied monotonic microseconds from ONE clock domain.
 * Elapsed intervals include waits; they are not CPU execution accounting.
 * They never establish actual GPU/display completion. */
typedef enum {
    TSFP_TIME_GUEST, TSFP_TIME_KERNEL, TSFP_TIME_RENDER_PREP,
    TSFP_TIME_GPU_SUBMIT, TSFP_TIME_LOAD_STALL, TSFP_TIME_DIAGNOSTIC_LOOP,
    TSFP_TIME_COUNT
} TsfpTimeKind;
typedef enum {
    TSFP_COUNT_SIMULATION, TSFP_COUNT_PRESENT_SUBMIT, TSFP_COUNT_DRAW,
    TSFP_COUNT_TEXTURE_UPLOAD, TSFP_COUNT_AUDIO_UNDERRUN, TSFP_COUNT_CLOCK_FAILURE,
    TSFP_COUNT_COUNT
} TsfpCounterKind;
typedef struct { uint64_t begin_us, end_us; TsfpTimeKind kind; } TsfpTraceSample;
typedef struct {
    TsfpTraceSample samples[TSFP_TRACE_CAPACITY];
    uint64_t counters[TSFP_COUNT_COUNT];
    uint64_t dropped, rejected, memory_high_water_bytes;
    uint32_t count, saturated_counters;
    int enabled;
} TsfpTelemetry;
typedef struct {
    uint32_t count;
    uint64_t minimum_us, median_us, p95_us, p99_us, maximum_us;
} TsfpTimeSummary;
/* Non-null arguments and an initialized, unmodified recorder are required.
 * Init/reset requires exclusive ownership; resets all samples/counters. */
void tsfp_telemetry_reset(TsfpTelemetry *trace, int enabled);
/* Retains first 256 valid intervals, then drops new intervals (no overwrites).
 * Disabled calls do nothing. Invalid kind/reversed clock increments rejected. */
int tsfp_telemetry_interval(TsfpTelemetry *trace, TsfpTimeKind kind,
                            uint64_t begin_us, uint64_t end_us);
void tsfp_telemetry_add(TsfpTelemetry *trace, TsfpCounterKind kind, uint64_t amount);
void tsfp_telemetry_memory(TsfpTelemetry *trace, uint64_t observed_bytes);
/* Nearest-rank percentiles of retained samples only; median is lower middle
 * for even counts. Empty/invalid query returns 0 and zeroes summary.
 * Bounded 2KiB local scratch; no allocation or recorder mutation. */
int tsfp_telemetry_summary(const TsfpTelemetry *trace, TsfpTimeKind kind,
                           TsfpTimeSummary *summary);
#endif
