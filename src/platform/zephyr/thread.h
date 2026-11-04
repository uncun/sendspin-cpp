// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <zephyr/kernel.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <utility>

namespace sendspin {

struct ZephyrThreadConfig {
    const char* name{"sendspin"};
    size_t stack_size{CONFIG_SENDSPIN_THREAD_STACK_SIZE};
    int priority{CONFIG_SENDSPIN_THREAD_PRIORITY};
};

inline k_spinlock thread_config_lock;
inline ZephyrThreadConfig next_thread_config;

// ESP priorities grow with urgency; Zephyr preemptive priorities grow in the opposite
// direction. Keep application priorities relative to the configurable Zephyr base.
inline void platform_configure_thread(const char* name, size_t stack_size, int priority, bool) {
    k_spinlock_key_t key = k_spin_lock(&thread_config_lock);
    next_thread_config = {name, std::max(stack_size, size_t(CONFIG_SENDSPIN_THREAD_STACK_SIZE)),
                          std::clamp(CONFIG_SENDSPIN_THREAD_PRIORITY + 5 - priority, 0,
                                     CONFIG_NUM_PREEMPT_PRIORITIES - 1)};
    k_spin_unlock(&thread_config_lock, key);
}

// The control block and stack stay at stable addresses across moves. Like std::thread,
// destroying a joinable thread is a programming error; owners signal stop before joining.
class PlatformThread {
public:
    PlatformThread() = default;
    template <class Function, class Argument>
    PlatformThread(Function function, Argument argument) {
        ZephyrThreadConfig config;
        {
            k_spinlock_key_t key = k_spin_lock(&thread_config_lock);
            config = next_thread_config;
            next_thread_config = {};
            k_spin_unlock(&thread_config_lock, key);
        }
        state_ = new State;
        state_->function = [function, argument] { function(argument); };
        state_->stack = k_thread_stack_alloc(config.stack_size, 0);
        if (state_->stack == nullptr) {
            std::abort();
        }
        k_thread_create(
            &state_->thread, state_->stack, config.stack_size,
            [](void* arg, void*, void*) { static_cast<State*>(arg)->function(); }, state_, nullptr,
            nullptr, config.priority, 0, K_FOREVER);
        k_thread_name_set(&state_->thread, config.name);
        k_thread_start(&state_->thread);
    }
    ~PlatformThread() {
        if (joinable()) {
            std::abort();
        }
    }
    PlatformThread(const PlatformThread&) = delete;
    PlatformThread& operator=(const PlatformThread&) = delete;
    PlatformThread(PlatformThread&& other) noexcept
        : state_(std::exchange(other.state_, nullptr)) {}
    PlatformThread& operator=(PlatformThread&& other) noexcept {
        if (joinable()) {
            std::abort();
        }
        state_ = std::exchange(other.state_, nullptr);
        return *this;
    }
    bool joinable() const {
        return state_ != nullptr;
    }
    void join() {
        if (state_ == nullptr) {
            return;
        }
        k_thread_join(&state_->thread, K_FOREVER);
        k_thread_stack_free(state_->stack);
        delete std::exchange(state_, nullptr);
    }

private:
    struct State {
        k_thread thread{};
        k_thread_stack_t* stack{nullptr};
        std::function<void()> function;
    };
    State* state_{nullptr};
};

template <class Rep, class Period>
inline void platform_sleep_for(std::chrono::duration<Rep, Period> duration) {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    if (us > 0) {
        k_sleep(K_USEC(us));
    }
}

}  // namespace sendspin
