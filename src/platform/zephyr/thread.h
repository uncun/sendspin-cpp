// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "platform/logging.h"
#include "platform/memory.h"
#include "platform/thread_config.h"
#include "thread_stack.h"
#include <zephyr/kernel.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <new>
#include <utility>

namespace sendspin {

// The control block and stack stay at stable addresses across moves. Like std::thread,
// destroying a joinable thread is a programming error; owners signal stop before joining.
// Allocation failure produces a non-joinable thread; callers must check before waiting.
class PlatformThread {
public:
    PlatformThread() = default;
    template <class Function, class Argument>
    PlatformThread(Function function, Argument argument)
        : PlatformThread(ThreadConfig{}, function, argument) {}
    template <class Function, class Argument>
    PlatformThread(const ThreadConfig& config, Function function, Argument argument) {
        const size_t stack_size =
            std::max(config.stack_size, size_t(CONFIG_SENDSPIN_THREAD_STACK_SIZE));
        // ESP priorities grow with urgency; Zephyr preemptive priorities grow in the opposite
        // direction.
        const int priority = std::clamp(CONFIG_SENDSPIN_THREAD_PRIORITY + 5 - config.priority, 0,
                                        CONFIG_NUM_PREEMPT_PRIORITIES - 1);
        // Keep the callable inline in the same fallible platform allocation as the
        // kernel control block; std::function can allocate a second, aborting block.
        struct Invocation : State {
            Function function;
            Argument argument;
            Invocation(Function entry, Argument arg)
                : function(std::move(entry)), argument(std::move(arg)) {}
        };
        void* memory = platform_malloc_internal(sizeof(Invocation));
        if (memory == nullptr) {
            SS_LOGE("sendspin.thread", "No control block for %s", config.name);
            return;
        }
        state_ = new (memory) Invocation(std::move(function), std::move(argument));
        state_->invoke = [](State* state) {
            auto* call = static_cast<Invocation*>(state);
            call->function(call->argument);
        };
        state_->release = [](State* state) {
            auto* call = static_cast<Invocation*>(state);
            call->~Invocation();
            platform_free(call);
        };
        state_->stack = zephyr_thread_stack_alloc(stack_size);
        if (state_->stack == nullptr) {
            SS_LOGE("sendspin.thread", "No stack for %s (%zu bytes)", config.name, stack_size);
            State* state = std::exchange(state_, nullptr);
            state->release(state);
            return;
        }
        k_thread_create(
            &state_->thread, state_->stack, stack_size,
            [](void* arg, void*, void*) {
                auto* state = static_cast<State*>(arg);
                state->invoke(state);
            },
            state_, nullptr, nullptr, priority, 0, K_FOREVER);
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
        zephyr_thread_stack_free(state_->stack);
        State* state = std::exchange(state_, nullptr);
        state->release(state);
    }

private:
    struct State {
        k_thread thread{};
        k_thread_stack_t* stack{nullptr};
        void (*invoke)(State*){nullptr};
        void (*release)(State*){nullptr};
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
