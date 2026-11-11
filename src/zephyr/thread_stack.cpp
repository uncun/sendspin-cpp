// SPDX-License-Identifier: Apache-2.0
#include "platform/zephyr/thread_stack.h"

#include <zephyr/sys/atomic.h>

#ifdef CONFIG_SENDSPIN_STATIC_THREAD_STACKS
#ifdef CONFIG_SENDSPIN_THREAD_STACKS_PSRAM
#include <esp_attr.h>
#endif
#endif

namespace sendspin {
#ifdef CONFIG_SENDSPIN_STATIC_THREAD_STACKS
namespace {
#ifdef CONFIG_SENDSPIN_THREAD_STACKS_PSRAM
Z_KERNEL_STACK_ARRAY_DEFINE_IN(stacks, CONFIG_SENDSPIN_THREAD_COUNT,
                               CONFIG_SENDSPIN_THREAD_STACK_SIZE, EXT_RAM_BSS_ATTR);
#else
K_KERNEL_STACK_ARRAY_DEFINE(stacks, CONFIG_SENDSPIN_THREAD_COUNT,
                            CONFIG_SENDSPIN_THREAD_STACK_SIZE);
#endif
atomic_t used[CONFIG_SENDSPIN_THREAD_COUNT];
}  // namespace

k_thread_stack_t* zephyr_thread_stack_alloc(size_t size) {
    if (size > CONFIG_SENDSPIN_THREAD_STACK_SIZE) {
        return nullptr;
    }
    for (size_t i = 0; i < ARRAY_SIZE(stacks); ++i) {
        if (atomic_cas(&used[i], 0, 1)) {
            return stacks[i];
        }
    }
    return nullptr;
}

void zephyr_thread_stack_free(k_thread_stack_t* stack) {
    for (size_t i = 0; i < ARRAY_SIZE(stacks); ++i) {
        if (stack == stacks[i]) {
            atomic_clear(&used[i]);
            return;
        }
    }
    __ASSERT_NO_MSG(false);
}
#else
k_thread_stack_t* zephyr_thread_stack_alloc(size_t size) {
    return k_thread_stack_alloc(size, 0);
}

void zephyr_thread_stack_free(k_thread_stack_t* stack) {
    k_thread_stack_free(stack);
}
#endif
}  // namespace sendspin
