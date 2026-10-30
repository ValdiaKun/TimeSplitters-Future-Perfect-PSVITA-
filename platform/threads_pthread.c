/* SPDX-License-Identifier: GPL-3.0-only */
#include "threads.h"
#include <errno.h>
#include <stdlib.h>
int tsfp_native_launch(TsfpNativeThread *thread,size_t stack,void *(*entry)(void *),void *context) {
    pthread_attr_t attr;
    int error=pthread_attr_init(&attr);
    if (error) return error;
    error=pthread_attr_setstacksize(&attr,stack);
    if (!error) error=pthread_create(&thread->handle,&attr,entry,context);
    if (pthread_attr_destroy(&attr)) abort();
    return error;
}
int tsfp_native_join(TsfpNativeThread *thread) {
    if (pthread_equal(thread->handle,pthread_self())) return EDEADLK;
    return pthread_join(thread->handle,NULL);
}
