/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_FAULT_REPORT_H
#define TSFP_FAULT_REPORT_H
#include "execution.h"
#define TSFP_FAULT_JSON_CAPACITY 640u
/* Format an already-captured event AFTER tsfp_guest_run has returned.
 * No guest memory reads, logging, explicit allocation or fault/recovery hooks.
 * Snapshot/output storage must be stable and exclusively owned by the caller.
 * Returns JSON byte count excluding NUL; 0 on invalid event/insufficient output.
 * On failure output is unchanged; capacity includes terminating NUL.
 * Integer diagnostics only: not a complete CPU save-state or Xbox SEH frame. */
size_t tsfp_fault_json(const TsfpFault *fault, char *output, size_t capacity);
#endif
