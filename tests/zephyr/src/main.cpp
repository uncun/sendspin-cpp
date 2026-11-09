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
    platform_configure_thread("SsTest", 4096, 5, false);
    PlatformThread worker(
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

ZTEST(sendspin_zephyr, test_flags_timeout_and_clear) {
    EventFlags flags;
    flags.create();
    const int64_t before = platform_time_us();
    zassert_equal(flags.wait(1, false, false, 20) & 1, 0);
    zassert_true(platform_time_us() - before >= 19000);
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
    detail::CycleClock32 clock;
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

ZTEST_SUITE(sendspin_zephyr, nullptr, nullptr, nullptr, nullptr, nullptr);
