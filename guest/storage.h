/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_STORAGE_H
#define TSFP_STORAGE_H
#include "worker.h"
/* Explicit image-BSS pool, not a production heap/memory budget. Configuration
 * requires externally serialized image lifecycle and no live workers/handles. */
typedef struct TsfpStorageConfig {
    uint32_t base,stride; unsigned slots;
    size_t native_stack_size;
    TsfpImageResolver resolve;
} TsfpStorageConfig;
typedef struct TsfpStorageRequest { uint32_t id,stack_size,entry,context1,context2; } TsfpStorageRequest;
enum TsfpStorageState { TSFP_STORAGE_PREPARED=1,TSFP_STORAGE_LAUNCHING,TSFP_STORAGE_RUNNING,TSFP_STORAGE_JOINING,TSFP_STORAGE_JOINED,TSFP_STORAGE_RELEASING };
typedef struct TsfpStorageInfo { TsfpWorkerSpec spec; uint32_t owner; enum TsfpStorageState state; int platform_error; } TsfpStorageInfo;
enum TsfpStorageError { TSFP_STORAGE_OK,TSFP_STORAGE_INVALID,TSFP_STORAGE_BUSY,TSFP_STORAGE_EXHAUSTED,TSFP_STORAGE_ALLOCATION,TSFP_STORAGE_INITIALIZATION,TSFP_STORAGE_LAUNCH_FAILED };
/* Allocation callbacks are native, non-reentrant and must not cancel/unwind. */
int tsfp_storage_set_allocator(void *(*allocate)(size_t),void (*release)(void *));
enum TsfpStorageError tsfp_storage_configure(const TsfpStorageConfig *);
int tsfp_storage_disable(void);
/* Native APIs, outside recovery. Failed reserve leaves output/RAM unchanged.
 * Cancel and failed launch restore pre-reservation metadata. Successful launch
 * commits initialization; release never erases guest stack contents or saves. */
enum TsfpStorageError tsfp_storage_reserve(const TsfpStorageRequest *,uint32_t *lease);
enum TsfpStorageError tsfp_storage_launch(uint32_t lease,int *platform_error);
int tsfp_storage_cancel(uint32_t lease);
int tsfp_storage_query(uint32_t lease,TsfpStorageInfo *);
enum TsfpWorkerError tsfp_storage_join(uint32_t lease,TsfpWorkerResult *);
int tsfp_storage_release(uint32_t lease);
int tsfp_storage_configured(void);
typedef struct TsfpStorageProfile { uint32_t initial_id,max_stack; size_t thread_data_size,native_stack_size; } TsfpStorageProfile;
/* Native service pin protects pool/configuration through service-thread join. */
int tsfp_storage_service_pin(TsfpStorageProfile *);
int tsfp_storage_service_unpin(void);
int tsfp_storage_output_allowed(uint32_t address,size_t size);
int tsfp_storage_kernel_output_allowed(uint32_t address,size_t size);
/* Internal ownership guard shared with raw worker/handle creation. Never call
 * guest-faulting APIs while locked; all ranges/policies must already be stable.
 * Lock order: ownership -> handle registry. No guest callbacks under this lock. */
int tsfp_storage_range_allowed_locked(uint32_t base,size_t size);
void tsfp_storage_lock(void);
void tsfp_storage_unlock(void);
int tsfp_storage_worker_allowed_locked(const TsfpWorker *,const TsfpWorkerSpec *);
int tsfp_storage_object_allowed_locked(uint32_t object,uint32_t id,uint32_t lease);
int tsfp_worker_range_busy_locked(uint32_t base,size_t size);
#endif
