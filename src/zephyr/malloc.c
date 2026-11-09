/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>
#include <zephyr/kernel.h>

void* __wrap_malloc(size_t size) {
    return k_malloc(size);
}

void* __wrap_calloc(size_t count, size_t size) {
    return k_calloc(count, size);
}

void* __wrap_realloc(void* pointer, size_t size) {
    return k_realloc(pointer, size);
}

void __wrap_free(void* pointer) {
    k_free(pointer);
}
