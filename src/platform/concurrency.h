// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <mutex>

#ifdef __ZEPHYR__
#include <zephyr/kernel.h>

namespace sendspin {

class PlatformMutex {
public:
    PlatformMutex() {
        k_mutex_init(&mutex_);
    }
    PlatformMutex(const PlatformMutex&) = delete;
    PlatformMutex& operator=(const PlatformMutex&) = delete;
    void lock() {
        k_mutex_lock(&mutex_, K_FOREVER);
    }
    void unlock() {
        k_mutex_unlock(&mutex_);
    }
    bool try_lock() {
        return k_mutex_lock(&mutex_, K_NO_WAIT) == 0;
    }
    k_mutex* native_handle() {
        return &mutex_;
    }

private:
    k_mutex mutex_{};
};

class PlatformConditionVariable {
public:
    PlatformConditionVariable() {
        k_condvar_init(&condition_);
    }
    PlatformConditionVariable(const PlatformConditionVariable&) = delete;
    PlatformConditionVariable& operator=(const PlatformConditionVariable&) = delete;
    void notify_all() {
        k_condvar_broadcast(&condition_);
    }

    template <class Predicate>
    void wait(std::unique_lock<PlatformMutex>& lock, Predicate ready) {
        while (!ready()) {
            k_condvar_wait(&condition_, lock.mutex()->native_handle(), K_FOREVER);
        }
    }

    template <class Rep, class Period, class Predicate>
    bool wait_for(std::unique_lock<PlatformMutex>& lock,
                  std::chrono::duration<Rep, Period> duration, Predicate ready) {
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        const auto deadline = sys_timepoint_calc(K_USEC(us > 0 ? us : 0));
        while (!ready()) {
            const k_timeout_t remaining = sys_timepoint_timeout(deadline);
            if (K_TIMEOUT_EQ(remaining, K_NO_WAIT)) {
                return ready();
            }
            k_condvar_wait(&condition_, lock.mutex()->native_handle(), remaining);
        }
        return true;
    }

private:
    k_condvar condition_{};
};

}  // namespace sendspin
#else
#include <condition_variable>
namespace sendspin {
using PlatformMutex = std::mutex;
using PlatformConditionVariable = std::condition_variable;
}  // namespace sendspin
#endif
