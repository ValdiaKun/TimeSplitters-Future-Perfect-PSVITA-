/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_HANDLES_H
#define TSFP_HANDLES_H
#include <stdint.h>
#define TSFP_THREAD_HANDLES 32u
#define TSFP_STATUS_INVALID_HANDLE UINT32_C(0xc0000008)
enum TsfpThreadLife { TSFP_THREAD_CREATED=1, TSFP_THREAD_RUNNING, TSFP_THREAD_TERMINATED };
typedef struct TsfpThreadInfo { uint32_t object,id,status; enum TsfpThreadLife state; int handle_open; unsigned waiters; int native_pin; } TsfpThreadInfo;
/* Caller owns/pins guest object/stack storage until release succeeds, and must
 * join/stop native execution before finish/release. No native thread is created.
 * Image initialization/reset and owner creation require external lifecycle
 * serialization; native owners may otherwise close/query/finish concurrently.
 * One guest handle per record; duplication is not implemented. */
int tsfp_thread_owner_create(uint32_t object,uint32_t id,uint32_t *token);
/* Platform owner pin persists through completion until actual native join. */
int tsfp_thread_owner_native_pin(uint32_t token);
int tsfp_thread_owner_native_unpin(uint32_t token);
int tsfp_thread_owner_start(uint32_t token);
int tsfp_thread_owner_finish(uint32_t token,uint32_t status);
int tsfp_thread_owner_query(uint32_t token,TsfpThreadInfo *info);
int tsfp_thread_owner_release(uint32_t token);
uint32_t tsfp_thread_handle_close(uint32_t handle);
/* Zero-timeout, non-alertable poll; completion is sticky until close. */
uint32_t tsfp_thread_handle_poll(uint32_t handle);
typedef struct TsfpWaitTimeout { uint64_t ticks; int infinite; } TsfpWaitTimeout;
/* Guest NT timeout bit pattern. Positive absolute deadlines are unsupported.
 * Output is unchanged on failure; negative INT64_MIN is handled unsigned. */
int tsfp_thread_timeout_decode(int present,uint64_t bits,TsfpWaitTimeout *out);
/* Non-alertable wait. Native cancellation is deferred until references/locks
 * are released; no guest access, callbacks or recovery occurs while waiting.
 * Closing after acquisition does not cancel a wait; release requires no waiters.
 * Completion wins a deadline tie when observed under the registry mutex. */
uint32_t tsfp_thread_handle_wait(uint32_t handle,const TsfpWaitTimeout *timeout);
int tsfp_thread_handles_busy(void);
#endif
