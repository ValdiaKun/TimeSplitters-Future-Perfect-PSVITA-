/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_PROFILED_GUEST_RUN_H
#define TSFP_PROFILED_GUEST_RUN_H
#include "execution.h"
#include "telemetry.h"
/* A clock returns nonzero and a monotonic microsecond timestamp on success.
 * It must not execute/fault into guest code or change recorder configuration.
 * Reads occur outside the outermost guest recovery boundary, with no owned
 * resources carried into translated execution. */
typedef int (*TsfpProfileClock)(void *context, uint64_t *time_us);
/* Optional host-entry adapter; does not change tsfp_guest_run or enable tracing
 * globally. Same callback/resource/cancellation restrictions as that API.
 * Recorder belongs exclusively to this native owner throughout the call.
 * Fault, recorder and mutable clock-context storage must be stable/disjoint.
 * Disabled tracing executes normally without consulting clock (which may be NULL).
 * Returns -1 for missing required inputs/clock or an active recovery boundary,
 * without invoking callback/clock or changing the snapshot/recorder. Otherwise
 * returns the existing 0=normal,1=captured event result, even if clock reads fail.
 * Clock-pair failure increments CLOCK_FAILURE without publishing a fake interval.
 * Reversed timestamps use the recorder's rejection accounting. Elapsed duration
 * includes native waits, scheduling, capture and guard cleanup; not CPU time. */
int tsfp_guest_run_profiled(void (*callback)(void *),void *context,TsfpFault *fault,
                            TsfpTelemetry *trace,TsfpProfileClock clock,void *clock_context);
#endif
