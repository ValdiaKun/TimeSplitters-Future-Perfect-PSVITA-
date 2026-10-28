#include "diagnostics.h"
#include <inttypes.h>
#include <stdio.h>
void tsfp_diagnostic(uint64_t time_us, const char *level, const char *event, int code) {
    fprintf(stderr, "tsfp time_us=%" PRIu64 " level=%s event=%s code=%d\n",
            time_us, level, event, code);
    fflush(stderr);
}
