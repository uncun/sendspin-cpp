// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <zephyr/kernel.h>

#include <cstdint>

namespace sendspin {
namespace detail {
// Extend a 32-bit hardware cycle counter. Coarse uptime disambiguates complete
// wraps even if nobody reads the clock for longer than one counter period.
class CycleClock32 {
public:
    uint64_t update(uint32_t cycles, uint64_t coarse_cycles) {
        if (!initialized_) {
            accumulated_ = cycles;
            initialized_ = true;
        } else {
            const uint64_t delta = static_cast<uint32_t>(cycles - previous_);
            const uint64_t approximate_delta = coarse_cycles - previous_coarse_;
            const uint64_t wraps = approximate_delta > delta
                                       ? (approximate_delta - delta + (uint64_t{1} << 31)) >> 32
                                       : 0;
            accumulated_ += delta + (wraps << 32);
        }
        previous_ = cycles;
        previous_coarse_ = coarse_cycles;
        return accumulated_;
    }

private:
    uint64_t accumulated_{0};
    uint64_t previous_coarse_{0};
    uint32_t previous_{0};
    bool initialized_{false};
};
}  // namespace detail

inline int64_t platform_time_us() {
#ifdef CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER
    return static_cast<int64_t>(k_cyc_to_us_floor64(k_cycle_get_64()));
#else
    static k_spinlock lock{};
    static detail::CycleClock32 clock;
    const k_spinlock_key_t key = k_spin_lock(&lock);
    const uint64_t coarse = k_ticks_to_cyc_floor64(k_uptime_ticks());
    const uint64_t cycles = clock.update(k_cycle_get_32(), coarse);
    k_spin_unlock(&lock, key);
    return static_cast<int64_t>(k_cyc_to_us_floor64(cycles));
#endif
}
}  // namespace sendspin
