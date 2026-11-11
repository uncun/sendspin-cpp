// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#include "websocket_reader.h"
#include <zephyr/net/socket.h>
#include <zephyr/sys/fdtable.h>
#include <zephyr/ztest.h>
extern "C" {
#include "websocket_internal.h"
}

#include <cstring>

using namespace sendspin;

// Zephyr's NET_TEST source returns EAGAIN when the supplied TCP prefix runs out.
// The production parser and websocket_recv_msg() run unchanged, including partial timeouts.
static void check_paused_frame(uint8_t* prefix, size_t prefix_size, size_t prefix_payload,
                               uint8_t* tail, size_t tail_size, const uint8_t* expected) {
    uint8_t scratch[1400];
    uint8_t output[2048];
    websocket_context ctx{};
    ctx.recv_buf.buf = scratch;
    ctx.recv_buf.size = sizeof(scratch);
    test_data input{};
    input.ctx = &ctx;
    input.input_buf = prefix;
    input.input_len = prefix_size;
    const int fd = zvfs_reserve_fd();
    zassert_true(fd >= 0);
    zvfs_finalize_fd(fd, &input, nullptr);
    WebSocketReader reader;
    uint32_t type = 0;
    uint64_t remaining = 0;
    size_t size = 0;
    const size_t total = prefix_payload + tail_size;
    while (size < prefix_payload) {
        const int count = reader.receive(fd, output + size, 1024, &type, &remaining, 100);
        zassert_true(count > 0);
        size += count;
        zassert_equal(type, WEBSOCKET_FLAG_BINARY | WEBSOCKET_FLAG_FINAL);
        zassert_equal(remaining, total - size, "a TCP pause must not end the frame");
    }
    zassert_equal(reader.receive(fd, output + size, 1024, &type, &remaining, 100), -EAGAIN);
    input.input_buf = tail;
    input.input_len = tail_size;
    input.input_pos = 0;
    while (size < total) {
        const int count = reader.receive(fd, output + size, 1024, &type, &remaining, 100);
        zassert_true(count > 0);
        size += count;
        zassert_equal(type, WEBSOCKET_FLAG_BINARY | WEBSOCKET_FLAG_FINAL);
        zassert_equal(remaining, total - size);
    }
    zassert_mem_equal(output, expected, total);
    uint8_t empty[] = {0x89, 0, 0x82, 0};
    input.input_buf = empty;
    input.input_len = sizeof(empty);
    input.input_pos = 0;
    zassert_equal(reader.receive(fd, output, sizeof(output), &type, &remaining, 100), 0);
    zassert_equal(type, WEBSOCKET_FLAG_PING | WEBSOCKET_FLAG_FINAL);
    zassert_equal(remaining, 0);
    zassert_equal(reader.receive(fd, output, sizeof(output), &type, &remaining, 100), 0);
    zassert_equal(type, WEBSOCKET_FLAG_BINARY | WEBSOCKET_FLAG_FINAL);
    zassert_equal(remaining, 0);
    zvfs_free_fd(fd);
}

ZTEST(websocket_rx, test_pause_before_first_receive_completes) {
    uint8_t prefix[] = {0x82, 0x84, 1, 2, 3, 4, 'a' ^ 1, 'b' ^ 2};
    uint8_t tail[] = {'c' ^ 3, 'd' ^ 4};
    const uint8_t expected[] = {'a', 'b', 'c', 'd'};
    check_paused_frame(prefix, sizeof(prefix), 2, tail, sizeof(tail), expected);
}

ZTEST(websocket_rx, test_pause_after_full_receive_buffer) {
    uint8_t prefix[4 + 1200] = {0x82, 126, 8, 0};
    uint8_t tail[848];
    uint8_t expected[2048];
    for (size_t i = 0; i < sizeof(expected); ++i) {
        expected[i] = i % 251;
    }
    std::memcpy(prefix + 4, expected, 1200);
    std::memcpy(tail, expected + 1200, sizeof(tail));
    check_paused_frame(prefix, sizeof(prefix), 1200, tail, sizeof(tail), expected);
}

extern "C" int verify_sent_and_received_msg(struct net_msghdr*, bool) {
    zassert_unreachable("receive fixture must not send");
    return -EIO;
}
ZTEST_SUITE(websocket_rx, nullptr, nullptr, nullptr, nullptr, nullptr);
