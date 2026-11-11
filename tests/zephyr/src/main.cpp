// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#include "platform/event_flags.h"
#include "platform/memory.h"
#include "platform/shared_ring_buffer.h"
#include "platform/thread.h"
#include "platform/time.h"
#include "sendspin/client.h"
#include <zephyr/ztest.h>

#include <cstring>
#include <utility>

using namespace sendspin;

ZTEST(sendspin_zephyr, test_thread_move_join_and_flags) {
    EventFlags flags;
    zassert_true(flags.create());
    auto worker = platform_create_thread(
        {.name = "SsTest", .stack_size = 4096, .priority = 5},
        [](EventFlags* state) {
            state->wait(1, true, true, UINT32_MAX);
            state->set(2);
        },
        &flags);
    PlatformThread moved(std::move(worker));
    zassert_false(worker.joinable());
    flags.set(1);
    zassert_equal(flags.wait(2, true, true, 1000) & 2, 2);
    moved.join();
    zassert_false(moved.joinable());
}

namespace {
struct ThreadObservation {
    int priority{0};
    const char* name{nullptr};
    EventFlags done;
};
void observe_thread(ThreadObservation* observed) {
    observed->priority = k_thread_priority_get(k_current_get());
    observed->name = k_thread_name_get(k_current_get());
    observed->done.set(1);
    observed->done.wait(2, false, false, UINT32_MAX);
}
}  // namespace

ZTEST(sendspin_zephyr, test_concurrent_thread_creation_keeps_configuration) {
    ThreadObservation first;
    ThreadObservation second;
    first.done.create();
    second.done.create();
    EventFlags ready;
    ready.create();
    // One caller waits while another creates a child; pending configuration must stay local.
    auto parent = platform_create_thread(
        {.name = "Parent", .stack_size = 4096, .priority = 5},
        [&](ThreadObservation* observed) {
            const ThreadConfig config{.name = "CallerA", .stack_size = 4096, .priority = 5};
            ready.set(1);
            ready.wait(2, false, false, UINT32_MAX);
            auto worker = platform_create_thread(config, observe_thread, observed);
            observed->done.wait(1, false, false, UINT32_MAX);
            ready.set(4);
            worker.join();
        },
        &first);
    ready.wait(1, false, false, UINT32_MAX);
    auto worker = platform_create_thread({.name = "CallerB", .stack_size = 4096, .priority = 7},
                                         observe_thread, &second);
    second.done.wait(1, false, false, UINT32_MAX);
    ready.set(2);
    ready.wait(4, false, false, UINT32_MAX);
    // Inspect before exit: the kernel-owned names must remain alive.
    zassert_equal(first.priority, CONFIG_SENDSPIN_THREAD_PRIORITY);
    zassert_equal(second.priority, CONFIG_SENDSPIN_THREAD_PRIORITY - 2);
    zassert_equal(std::strcmp(first.name, "CallerA"), 0);
    zassert_equal(std::strcmp(second.name, "CallerB"), 0);
    first.done.set(2);
    second.done.set(2);
    worker.join();
    parent.join();
}

ZTEST(sendspin_zephyr, test_flags_timeout_and_clear) {
    EventFlags flags;
    flags.create();
    flags.set(2);
    zassert_equal(flags.wait(1, false, false, 20) & 1, 0);
    zassert_equal(flags.get() & 2, 2);
    flags.set(3);
    zassert_equal(flags.wait(3, true, true, 0) & 3, 3);
    zassert_equal(flags.wait(3, false, false, 0) & 3, 0);
}

ZTEST(sendspin_zephyr, test_ring_out_of_order_returns) {
    alignas(8) uint8_t storage[128]{};
    SharedRingBuffer ring;
    zassert_true(ring.create(sizeof(storage), storage));
    void* first = ring.acquire(32, 0);
    void* second = ring.acquire(32, 0);
    zassert_not_null(first);
    zassert_not_null(second);
    std::memset(first, 0x11, 32);
    std::memset(second, 0x22, 32);
    ring.complete(second);
    size_t size = 0;
    zassert_is_null(ring.take(&size, 0));
    ring.complete(first);
    zassert_equal_ptr(ring.take(&size, 0), first);
    zassert_equal_ptr(ring.take(&size, 0), second);
    ring.return_item(second);
    zassert_is_null(ring.acquire(48, 0));
    ring.return_item(first);
    zassert_not_null(ring.acquire(48, 0));
}

class OfflineNetwork final : public SendspinNetworkProvider {
public:
    bool is_network_ready() override {
        return false;
    }
};

ZTEST(sendspin_zephyr, test_client_restart) {
    OfflineNetwork network;
    SendspinClientConfig config;
    config.name = "Zephyr lifecycle test";
    SendspinClient client(config);
    client.set_network_provider(&network);
    client.add_metadata();
    client.add_controller();
    for (int i = 0; i < 3; ++i) {
        zassert_true(client.start());
        client.loop();
        k_sleep(K_MSEC(10));
        client.stop();
    }
}

ZTEST(sendspin_zephyr, test_cycle_counter_wrap_and_idle) {
    sendspin::detail::CycleClock32 clock;
    const uint64_t start = 0xfffffff0U;
    zassert_equal(clock.update(start, 0), start);
    zassert_equal(clock.update(0x10, 32), start + 32);
    // More than two complete counter periods pass without a read.
    const uint64_t idle = (uint64_t{2} << 32) + 100;
    zassert_equal(clock.update(0x74, 32 + idle), start + 32 + idle);
    // A coarse tick boundary may put uptime slightly ahead of the cycle delta.
    zassert_equal(clock.update(0x78, 32 + idle + 1000), start + 32 + idle + 4);
}

ZTEST(sendspin_zephyr, test_buffer_heap_realloc_and_exhaustion) {
    platform_free(nullptr);
    auto* data = static_cast<uint8_t*>(platform_realloc(nullptr, 128));
    zassert_not_null(data);
    std::memset(data, 0x5a, 128);
    auto* grown = static_cast<uint8_t*>(platform_realloc(data, 256));
    zassert_not_null(grown);
    for (size_t i = 0; i < 128; ++i) {
        zassert_equal(grown[i], 0x5a);
    }
#if CONFIG_SENDSPIN_BUFFER_HEAP_SIZE > 0
    zassert_is_null(platform_realloc(grown, CONFIG_SENDSPIN_BUFFER_HEAP_SIZE * 2));
    zassert_equal(grown[0], 0x5a);
#endif
    platform_free(grown);
    void* internal = platform_malloc_internal(128);
    zassert_not_null(internal);
    platform_free(internal);
}

#if CONFIG_SENDSPIN_BUFFER_HEAP_SIZE > 0
ZTEST(sendspin_zephyr, test_thread_refuses_exhausted_control_blocks_and_can_retry) {
    EventFlags done;
    done.create();
    // Each allocation is smaller than a worker control block, so any remainder
    // cannot hide a successful worker allocation after the loop fills this heap.
    void* blocks[CONFIG_SENDSPIN_BUFFER_HEAP_SIZE / sizeof(k_thread)];
    size_t count = 0;
    while (count < std::size(blocks)) {
        void* block = platform_malloc_internal(sizeof(k_thread));
        if (block == nullptr) {
            break;
        }
        blocks[count++] = block;
    }
    const auto signal = [](EventFlags* flags) { flags->set(1); };
    PlatformThread worker(signal, &done);
    zassert_false(worker.joinable());
    for (size_t i = 0; i < count; ++i) {
        platform_free(blocks[i]);
    }
    // Control: allocation failure must leave the object reusable.
    worker = PlatformThread(signal, &done);
    zassert_true(worker.joinable());
    done.wait(1, false, false, UINT32_MAX);
    worker.join();
}
#endif

#ifdef CONFIG_SENDSPIN_STATIC_THREAD_STACKS
ZTEST(sendspin_zephyr, test_stack_pool_bounds_and_reuse) {
    k_thread_stack_t* stacks[CONFIG_SENDSPIN_THREAD_COUNT];
    zassert_is_null(zephyr_thread_stack_alloc(CONFIG_SENDSPIN_THREAD_STACK_SIZE + 1));
    for (auto& stack : stacks) {
        stack = zephyr_thread_stack_alloc(CONFIG_SENDSPIN_THREAD_STACK_SIZE);
        zassert_not_null(stack);
    }
    zassert_is_null(zephyr_thread_stack_alloc(CONFIG_SENDSPIN_THREAD_STACK_SIZE));
    for (auto* stack : stacks) {
        zephyr_thread_stack_free(stack);
    }
    auto* reused = zephyr_thread_stack_alloc(CONFIG_SENDSPIN_THREAD_STACK_SIZE);
    zassert_not_null(reused);
    zephyr_thread_stack_free(reused);
}

ZTEST(sendspin_zephyr, test_client_refuses_exhausted_stacks_and_can_restart) {
    OfflineNetwork network;
    SendspinClient client(SendspinClientConfig{});
    client.set_network_provider(&network);
    k_thread_stack_t* stacks[CONFIG_SENDSPIN_THREAD_COUNT];
    for (auto& stack : stacks) {
        stack = zephyr_thread_stack_alloc(CONFIG_SENDSPIN_THREAD_STACK_SIZE);
        zassert_not_null(stack);
    }
    // No protocol worker can be created. start() must return false instead of
    // waiting for an idle notification from a thread that does not exist.
    zassert_false(client.start());
    for (auto* stack : stacks) {
        zephyr_thread_stack_free(stack);
    }
    // Control: a refused start must release its bindings so the same client can retry.
    zassert_true(client.start());
    client.stop();
}

ZTEST(sendspin_zephyr, test_workers_with_kernel_heap_pressure) {
    // Match the application heap: C++ objects and networking leave too little
    // room for even one dynamic worker stack. Reserved stacks must still work.
    void* pressure = k_malloc(CONFIG_HEAP_MEM_POOL_SIZE - 11000);
    zassert_not_null(pressure);
    auto* dynamic = k_thread_stack_alloc(CONFIG_SENDSPIN_THREAD_STACK_SIZE, 0);
    zassert_is_null(dynamic);
    for (int cycle = 0; cycle < 2; ++cycle) {
        EventFlags flags;
        flags.create();
        PlatformThread workers[3];
        for (auto& worker : workers) {
            worker = PlatformThread(
                [](EventFlags* ready) { ready->wait(1, false, false, UINT32_MAX); }, &flags);
        }
        flags.set(1);
        for (auto& worker : workers) {
            worker.join();
        }
    }
    k_free(pressure);
}
#endif

ZTEST_SUITE(sendspin_zephyr, nullptr, nullptr, nullptr, nullptr, nullptr);
