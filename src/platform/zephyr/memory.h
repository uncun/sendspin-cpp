/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <cstddef>
namespace sendspin {
void* platform_malloc(size_t);
void* platform_realloc(void*, size_t);
void* platform_malloc_internal(size_t);
void* platform_realloc_internal(void*, size_t);
void platform_free(void*);
}  // namespace sendspin
