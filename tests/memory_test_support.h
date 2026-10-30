/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TSFP_MEMORY_TEST_SUPPORT_H
#define TSFP_MEMORY_TEST_SUPPORT_H
#include <stddef.h>
#include <stdint.h>
void test_expect_fault(void (*operation)(void *), void *context,
                       uint32_t address, size_t width);
void test_expect_memory_fault(uint32_t address, size_t width, int write);
#endif
