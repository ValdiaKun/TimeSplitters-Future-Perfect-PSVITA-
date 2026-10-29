/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_HANDLES_H
#define TSFP_HANDLES_H
#include <stdint.h>
#define TSFP_THREAD_HANDLES 32u
#define TSFP_STATUS_INVALID_HANDLE UINT32_C(0xc0000008)
enum TsfpThreadLife { TSFP_THREAD_CREATED=1, TSFP_THREAD_RUNNING, TSFP_THREAD_TERMINATED };
typedef struct TsfpThreadInfo { uint32_t object,id,status; enum TsfpThreadLife state; int handle_open; } TsfpThreadInfo;
/* Caller owns/pins guest object/stack storage until release succeeds, and must
 * join/stop native execution before finish/release. No native thread is created.
 * Image initialization/reset and owner creation require external lifecycle
 * serialization; native owners may otherwise close/query/finish concurrently.
 * One guest handle per record; duplicate/wait imports are not implemented. */
int tsfp_thread_owner_create(uint32_t object,uint32_t id,uint32_t *token);
int tsfp_thread_owner_start(uint32_t token);
int tsfp_thread_owner_finish(uint32_t token,uint32_t status);
int tsfp_thread_owner_query(uint32_t token,TsfpThreadInfo *info);
int tsfp_thread_owner_release(uint32_t token);
uint32_t tsfp_thread_handle_close(uint32_t handle);
int tsfp_thread_handles_busy(void);
#endif
