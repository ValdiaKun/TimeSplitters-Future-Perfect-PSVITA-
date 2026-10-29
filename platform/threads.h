/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_PLATFORM_THREADS_H
#define TSFP_PLATFORM_THREADS_H
#include <stddef.h>
#include <pthread.h>
/* Public POSIX backend, including VitaSDK's pthread implementation. Native
 * stacks are backend-owned until join; no guest stack is used as a native stack. */
typedef struct TsfpNativeThread { pthread_t handle; } TsfpNativeThread;
int tsfp_native_launch(TsfpNativeThread *,size_t stack_bytes,void *(*entry)(void *),void *context);
int tsfp_native_join(TsfpNativeThread *);
#endif
