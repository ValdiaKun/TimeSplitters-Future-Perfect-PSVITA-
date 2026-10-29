/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_WORKER_H
#define TSFP_WORKER_H
#include "image.h"
#include "../platform/threads.h"
typedef struct TsfpWorkerSpec {
    uint32_t object,id,stack,tib,entry,context1,context2;
    uint32_t storage_lease; /* zero for caller-owned storage */
    size_t stack_size,tls_size,native_stack_size;
    TsfpImageResolver resolve;
} TsfpWorkerSpec;
typedef struct TsfpWorkerResult { uint32_t status; int event; TsfpFault fault; TsfpCpuState cpu; } TsfpWorkerResult;
/* Zero-initialize, keep alive until release. API serializes concurrent operations
 * on a worker. Output/spec native buffers are trusted, distinct and accessible.
 * Caller prepares private TLS/object/storage before launch and serializes image
 * setup/teardown. The owner token is published only on successful launch;
 * only this adapter may start/finish/unpin its owners. Guest close/wait remains
 * independent. Launch/join/release are forbidden in guest recovery scopes. */
typedef struct TsfpWorker {
    TsfpNativeThread native;
    TsfpWorkerSpec spec;
    TsfpWorkerResult result;
    TsfpImageFunction function;
    uint32_t owner;
    unsigned state;
    int platform_error; /* pthread/backend error code; zero after successful join */
} TsfpWorker;
enum TsfpWorkerError { TSFP_WORKER_OK,TSFP_WORKER_INVALID,TSFP_WORKER_BUSY,TSFP_WORKER_RESOURCE,TSFP_WORKER_PLATFORM };
enum TsfpWorkerError tsfp_worker_launch(TsfpWorker *,const TsfpWorkerSpec *);
enum TsfpWorkerError tsfp_worker_join(TsfpWorker *,TsfpWorkerResult *);
/* Requires successful join, closed guest handle, no pending wait references. */
int tsfp_worker_release(TsfpWorker *);
#endif
