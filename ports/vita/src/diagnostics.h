#ifndef TSFP_VITA_DIAGNOSTICS_H
#define TSFP_VITA_DIAGNOSTICS_H
#include <stdint.h>
/* Single startup/main thread; event names are fixed tokens, never asset data. */
void tsfp_diagnostic(uint64_t time_us, const char *level, const char *event, int code);
#endif
