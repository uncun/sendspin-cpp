/* SPDX-License-Identifier: Apache-2.0 */
#include "platform/zephyr/memory.h"

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

#ifdef CONFIG_ESP_SPIRAM
#include <esp_attr.h>
#endif

namespace sendspin {
#ifdef CONFIG_ESP_SPIRAM
static uint8_t arena[CONFIG_SENDSPIN_BUFFER_HEAP_SIZE] EXT_RAM_BSS_ATTR __aligned(16);
#else
static uint8_t arena[CONFIG_SENDSPIN_BUFFER_HEAP_SIZE] __aligned(16);
#endif
static k_heap heap;
K_MUTEX_DEFINE(init_mutex);
static bool initialized;

void* platform_malloc(size_t size) {
    k_mutex_lock(&init_mutex, K_FOREVER);
    if (!initialized) {
        k_heap_init(&heap, arena, sizeof(arena));
        initialized = true;
    }
    k_mutex_unlock(&init_mutex);
    return k_heap_alloc(&heap, size, K_NO_WAIT);
}
void* platform_malloc_internal(size_t size) {
    return platform_malloc(size);
}
void platform_free(void* pointer) {
    if (pointer != nullptr) {
        k_heap_free(&heap, pointer);
    }
}
void* platform_realloc(void* pointer, size_t size) {
    if (!pointer) {
        return platform_malloc(size);
    }
    return k_heap_realloc(&heap, pointer, size, K_NO_WAIT);
}
void* platform_realloc_internal(void* pointer, size_t size) {
    return platform_realloc(pointer, size);
}
}  // namespace sendspin
