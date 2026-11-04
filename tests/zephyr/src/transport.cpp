// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#include "client_connection.h"
#include "platform/event_flags.h"
#include "platform/time.h"
#include "protocol_task.h"
#include <psa/crypto.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/base64.h>
#include <zephyr/ztest.h>

#include <cstring>
#include <string>

using namespace sendspin;

namespace {
struct Peer {
    int listener{-1};
    EventFlags flags;
    bool success{false};
};

bool read_request(int fd, std::string& request) {
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 2048) {
        zsock_pollfd poll{fd, ZSOCK_POLLIN, 0};
        if (zsock_poll(&poll, 1, 2000) <= 0) {
            return false;
        }
        char buffer[256];
        const int count = zsock_recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            return false;
        }
        request.append(buffer, count);
    }
    return request.find("GET /sendspin?test=1 HTTP/1.1\r\n") == 0;
}

bool upgrade(int fd) {
    std::string request;
    if (!read_request(fd, request)) {
        return false;
    }
    const std::string header = "Sec-WebSocket-Key: ";
    const size_t begin = request.find(header);
    if (begin == std::string::npos) {
        return false;
    }
    const size_t end = request.find("\r\n", begin);
    std::string key = request.substr(begin + header.size(), end - begin - header.size());
    key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[20];
    size_t digest_len = 0;
    if (psa_hash_compute(PSA_ALG_SHA_1, reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                         digest, sizeof(digest), &digest_len) != PSA_SUCCESS) {
        return false;
    }
    uint8_t accept[32]{};
    size_t length = 0;
    if (base64_encode(accept, sizeof(accept), &length, digest, sizeof(digest)) != 0) {
        return false;
    }
    const std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                 "Connection: Upgrade\r\nSec-WebSocket-Accept: " +
                                 std::string(reinterpret_cast<char*>(accept), length) + "\r\n\r\n";
    return zsock_send(fd, response.data(), response.size(), 0) == int(response.size());
}

bool exchange(int ws) {
    uint8_t received[32];
    uint32_t type = 0;
    uint64_t remaining = 0;
    int count = websocket_recv_msg(ws, received, sizeof(received), &type, &remaining, 2000);
    if (count != 3 || remaining != 0 || (type & WEBSOCKET_FLAG_BINARY) == 0 ||
        std::memcmp(received, "out", 3) != 0) {
        return false;
    }
    uint8_t large[3000];
    std::memset(large, 0x5a, sizeof(large));
    if (websocket_send_msg(ws, large, sizeof(large), WEBSOCKET_OPCODE_DATA_BINARY, false, true,
                           2000) != sizeof(large)) {
        return false;
    }
    const auto send = [ws](const char* text, websocket_opcode opcode, bool final) {
        const size_t len = std::strlen(text);
        return websocket_send_msg(ws, reinterpret_cast<const uint8_t*>(text), len, opcode, false,
                                  final, 2000) == int(len);
    };
    if (!send("hel", WEBSOCKET_OPCODE_DATA_TEXT, false) ||
        !send("probe", WEBSOCKET_OPCODE_PING, true)) {
        return false;
    }
    count = websocket_recv_msg(ws, received, sizeof(received), &type, &remaining, 2000);
    if (count != 5 || remaining != 0 || (type & WEBSOCKET_FLAG_PONG) == 0 ||
        std::memcmp(received, "probe", 5) != 0) {
        return false;
    }
    return send("lo", WEBSOCKET_OPCODE_CONTINUE, true) &&
           send("again", WEBSOCKET_OPCODE_DATA_TEXT, true);
}

void serve(Peer* peer) {
    zsock_pollfd poll{peer->listener, ZSOCK_POLLIN, 0};
    const int fd =
        zsock_poll(&poll, 1, 2000) > 0 ? zsock_accept(peer->listener, nullptr, nullptr) : -1;
    uint8_t buffer[512];
    int ws = -1;
    if (fd >= 0 && upgrade(fd)) {
        ws = websocket_register(fd, buffer, sizeof(buffer));
        if (ws >= 0) {
            peer->success = exchange(ws);
        }
    }
    peer->flags.set(1);
    peer->flags.wait(2, true, true, 3000);
    if (fd >= 0) {
        zsock_shutdown(fd, ZSOCK_SHUT_RDWR);
    }
    if (ws >= 0) {
        websocket_disconnect(ws);
    }
    if (fd >= 0) {
        zsock_close(fd);
    }
}
}  // namespace

ZTEST(sendspin_zephyr, test_websocket_frames_ping_and_fragmentation) {
    Peer peer;
    peer.flags.create();
    peer.listener = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    zassert_true(peer.listener >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    zassert_equal(zsock_inet_pton(AF_INET, "127.0.0.1", &address.sin_addr), 1);
    zassert_ok(zsock_bind(peer.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)));
    zassert_ok(zsock_listen(peer.listener, 1));
    socklen_t size = sizeof(address);
    zassert_ok(zsock_getsockname(peer.listener, reinterpret_cast<sockaddr*>(&address), &size));
    PlatformThread server(serve, &peer);
    ProtocolTask protocol(2);
    InboundRing ring;
    zassert_true(ring.create(16384, MemoryLocation::PREFER_INTERNAL, 4096));
    {
        SendspinClientConnection connection(
            "ws://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/sendspin?test=1");
        connection.attach_inbound(&ring, &protocol);
        connection.inbound_gate().set_admitted(true);
        connection.start();
        const int64_t deadline = platform_time_us() + 2000000;
        while (!connection.is_connected() && platform_time_us() < deadline) {
            k_sleep(K_MSEC(1));
        }
        zassert_true(connection.is_connected());
        zassert_equal(connection.send_binary_message(reinterpret_cast<const uint8_t*>("out"), 3),
                      SsErr::OK);
        zassert_equal(peer.flags.wait(1, false, true, 3000) & 1, 1);
        zassert_true(peer.success);
        size_t message_size = 0;
        void* item = ring.take(&message_size, 1000);
        zassert_not_null(item);
        zassert_equal(message_size, 3000);
        for (size_t i = 0; i < message_size; ++i) {
            zassert_equal(inbound_item_bytes(item)[i], 0x5a);
        }
        ring.return_item(item);
        for (const char* expected : {"hello", "again"}) {
            item = ring.take(&message_size, 1000);
            zassert_not_null(item);
            zassert_equal(message_size, 5);
            zassert_mem_equal(inbound_item_bytes(item), expected, 5);
            ring.return_item(item);
        }
        connection.detach_inbound();
        connection.close_transport_now();
        peer.flags.set(2);
    }
    server.join();
    zsock_close(peer.listener);
    ring.reset();
}

ZTEST(sendspin_zephyr, test_cancelled_and_unsupported_urls) {
    for (const char* url :
         {"wss://localhost/sendspin", "ws://", "ws://localhost:0/", "ws://[::1"}) {
        SendspinClientConnection connection(url);
        connection.start();
        connection.close_transport_now();
        zassert_false(connection.is_connected());
    }
}
