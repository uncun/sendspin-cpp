// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#include <zephyr/random/random.h>

#include <cstddef>
#include <cstdlib>

extern "C" void noise_rand_bytes(void* bytes, size_t size);
extern "C" void noise_rand_bytes(void* bytes, size_t size) {
    // No fallback to non-cryptographic randomness for pairing or session keys.
    if (sys_csrand_get(bytes, size) != 0) {
        std::abort();
    }
}
