// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

namespace sendspin {
struct ThreadConfig {
    const char* name{"sendspin"};
    size_t stack_size{0};
    int priority{5};
    bool stack_in_psram{false};
};
}  // namespace sendspin
