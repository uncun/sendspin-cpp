// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "connection.h"
#include "platform/concurrency.h"
#include "platform/thread.h"
#include <zephyr/net/socket.h>
#include <zephyr/net/websocket.h>

#include <atomic>
#include <string>

namespace sendspin {

// Outbound ws:// transport. Noise provides Sendspin's mandatory encryption.
class SendspinClientConnection : public SendspinConnection {
public:
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 30000;
    explicit SendspinClientConnection(std::string url);
    ~SendspinClientConnection() override;
    void start() override;
    void disconnect(SendspinGoodbyeReason reason) override;
    void close_transport_now() override;
    SsErr send_text_message(const char* text, size_t len) override;
    SsErr send_binary_message(const uint8_t* data, size_t len) override;
    bool is_connected() const override {
        return connected_.load();
    }
    bool is_outbound() const override {
        return true;
    }
    void set_task_config(unsigned priority, size_t stack_size) {
        priority_ = priority;
        stack_size_ = stack_size;
    }

private:
    struct ReceiveState;

    void run();
    bool open();
    bool connect_tcp(const std::string& host, const std::string& port);
    bool connect_address(const zsock_addrinfo& address);
    bool wait_for_connection(int fd);
    bool upgrade_websocket(const std::string& authority, const std::string& target);
    void close_sockets();

    void receive();
    bool receive_control_frame(ReceiveState& state, const uint8_t* data, size_t len,
                               uint32_t type, uint64_t remaining);
    bool receive_data_frame(ReceiveState& state, const uint8_t* data, size_t len,
                            uint32_t type, uint64_t remaining);
    bool receive_single_frame(ReceiveState& state, const uint8_t* data, size_t len,
                              bool frame_end);
    bool receive_fragment(const uint8_t* data, size_t len, bool first, bool text, bool last);
    SsErr send_frame(const uint8_t* data, size_t len, websocket_opcode opcode);

    std::string url_;
    PlatformThread thread_;
    PlatformMutex send_mutex_;
    int tcp_socket_{-1};
    int ws_socket_{-1};
    unsigned priority_{5};
    size_t stack_size_{CONFIG_SENDSPIN_THREAD_STACK_SIZE};
    // Must live for the entire WebSocket session, not just the HTTP upgrade.
    uint8_t http_buffer_[1024]{};
    std::atomic<bool> connected_{false};
    std::atomic<bool> stop_{false};
};

}  // namespace sendspin
