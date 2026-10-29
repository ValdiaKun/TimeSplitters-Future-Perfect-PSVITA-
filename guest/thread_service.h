/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_THREAD_SERVICE_H
#define TSFP_THREAD_SERVICE_H
#include <stdint.h>
#define TSFP_STATUS_NOT_SUPPORTED UINT32_C(0xc00000bb)
#define TSFP_STATUS_INVALID_PARAMETER UINT32_C(0xc000000d)
#define TSFP_STATUS_INSUFFICIENT_RESOURCES UINT32_C(0xc000009a)
typedef struct TsfpThreadCreateRequest { uint32_t stack_size,tls_size,entry,context1,context2; } TsfpThreadCreateRequest;
typedef struct TsfpThreadCreateResult { uint32_t status,handle,id; } TsfpThreadCreateResult;
/* Native lifecycle only, outside guest recovery. Explicit default guest stack;
 * native stack comes from the configured storage profile. Stop requires guest
 * callers to be quiescent; shutdown invalidates completed managed handles.
 * platform_error is written only when a native backend operation is attempted.
 * Stop refuses live or
 * referenced workers; completed handles are closed during explicit shutdown. */
int tsfp_thread_service_start(uint32_t default_stack,int *platform_error);
int tsfp_thread_service_stop(int *platform_error);
/* Guest callback enters after its complete import frame has been prefetched.
 * A durable scope guard and disabled native cancellation span argument probes,
 * RPC, output writes and register/stack publication. Leave LAST on normal exit. */
int tsfp_thread_service_call_enter(void);
void tsfp_thread_service_call_leave(void);
/* Native-only packet: no guest output pointers or caller stack pointers are
 * retained by the service. Creation runs on a different native C stack. */
TsfpThreadCreateResult tsfp_thread_service_create(const TsfpThreadCreateRequest *);
#endif
