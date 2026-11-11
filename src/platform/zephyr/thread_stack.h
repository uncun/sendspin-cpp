// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <zephyr/kernel.h>

namespace sendspin {
k_thread_stack_t* zephyr_thread_stack_alloc(size_t size);
void zephyr_thread_stack_free(k_thread_stack_t* stack);
}  // namespace sendspin
