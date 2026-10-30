/* SPDX-License-Identifier: GPL-3.0-only */
#include "fault_report.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
size_t tsfp_fault_json(const TsfpFault *fault, char *output, size_t capacity) {
    char buffer[TSFP_FAULT_JSON_CAPACITY];
    if (!fault || !output || (unsigned)fault->kind < TSFP_FAULT_MEMORY ||
        (unsigned)fault->kind > TSFP_FAULT_IMPORT || (unsigned)fault->access > TSFP_ACCESS_EXECUTE)
        return 0;
    const uint32_t *r = fault->cpu.registers;
    int count = snprintf(buffer, sizeof buffer,
        "{\"schema_version\":1,\"kind\":%u,\"access\":%u,\"pc\":%" PRIu32
        ",\"address\":%" PRIu32 ",\"width\":%zu,\"detail\":%" PRIu32
        ",\"thread_id\":%" PRIu32 ",\"registers\":[%" PRIu32 ",%" PRIu32
        ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
        "],\"flags\":%" PRIu32 ",\"flags_valid\":%" PRIu32 "}",
        (unsigned)fault->kind, (unsigned)fault->access, fault->cpu.pc,
        fault->address, fault->width, fault->detail, fault->thread_id,
        r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7],fault->cpu.flags,fault->cpu.flags_valid);
    if (count < 0 || (size_t)count >= sizeof buffer || (size_t)count >= capacity) return 0;
    memcpy(output, buffer, (size_t)count + 1);
    return (size_t)count;
}
