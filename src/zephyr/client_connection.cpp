// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#include "client_connection.h"

#include "platform/logging.h"
#include "platform/time.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace sendspin {
namespace {
constexpr int IO_TIMEOUT_MS = 1000;
constexpr int OPEN_TIMEOUT_MS = 5000;
constexpr int RECEIVE_TIMEOUT_MS = 100;
const char* const TAG = "sendspin.zephyr";

struct WebSocketUrl {
    std::string authority;
    std::string host;
    std::string port{"80"};
    std::string target;
};

bool parse_authority(WebSocketUrl& parsed) {
    const auto& authority = parsed.authority;
    if (authority.empty() || authority.find('@') != std::string::npos) {
        return false;
    }
    if (authority.front() == '[') {
        const size_t end = authority.find(']');
        if (end == std::string::npos) {
            return false;
        }
        parsed.host = authority.substr(1, end - 1);
        if (end + 1 < authority.size()) {
            if (authority[end + 1] != ':') {
                return false;
            }
            parsed.port = authority.substr(end + 2);
        }
    } else {
        const size_t colon = authority.find(':');
        parsed.host = authority.substr(0, colon);
        if (colon != std::string::npos) {
            parsed.port = authority.substr(colon + 1);
        }
    }
    if (parsed.host.empty() || parsed.port.empty() || parsed.port.size() > 5 ||
        parsed.port.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    const unsigned long port = std::strtoul(parsed.port.c_str(), nullptr, 10);
    return port > 0 && port <= 65535;
}

bool parse_url(const std::string& url, WebSocketUrl& parsed) {
    // Deliberately reject TLS, credentials and fragments. Bracketed IPv6 literals and
    // DNS names are accepted. A query belongs to the request target, not the authority.
    if (!url.starts_with("ws://") || url.find('#') != std::string::npos) {
        return false;
    }
    const size_t target_pos = url.find_first_of("/?#", 5);
    parsed.authority =
        url.substr(5, target_pos == std::string::npos ? std::string::npos : target_pos - 5);
    if (!parse_authority(parsed)) {
        return false;
    }
    parsed.target = target_pos == std::string::npos ? "/" : url.substr(target_pos);
    if (parsed.target.front() == '?') {
        parsed.target.insert(0, "/");
    }
    return true;
}
}  // namespace

SendspinClientConnection::SendspinClientConnection(std::string url) : url_(std::move(url)) {}

SendspinClientConnection::~SendspinClientConnection() {
    close_transport_now();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SendspinClientConnection::start() {
    if (thread_.joinable()) {
        return;
    }
    platform_configure_thread("SsWs", stack_size_, static_cast<int>(priority_), false);
    thread_ = PlatformThread([](SendspinClientConnection* self) { self->run(); }, this);
}

void SendspinClientConnection::close_transport_now() {
    // Never join or wait for a transport lock here: the inbound gate can call this on
    // the receive thread. The worker observes cancellation at its next bounded I/O wait.
    stop_ = true;
    connected_ = false;
}

void SendspinClientConnection::disconnect(SendspinGoodbyeReason reason) {
    if (is_connected()) {
        send_goodbye_reason(reason);
    }
    close_transport_now();
}

SsErr SendspinClientConnection::send_text_message(const char* text, size_t len) {
    return send_frame(reinterpret_cast<const uint8_t*>(text), len, WEBSOCKET_OPCODE_DATA_TEXT);
}

SsErr SendspinClientConnection::send_binary_message(const uint8_t* data, size_t len) {
    return send_frame(data, len, WEBSOCKET_OPCODE_DATA_BINARY);
}

SsErr SendspinClientConnection::send_frame(const uint8_t* data, size_t len,
                                           websocket_opcode opcode) {
    std::lock_guard<PlatformMutex> lock(send_mutex_);
    if (!connected_ || stop_) {
        return SsErr::INVALID_STATE;
    }
    const int sent = websocket_send_msg(ws_socket_, data, len, opcode, true, true, IO_TIMEOUT_MS);
    if (sent < 0 || static_cast<size_t>(sent) != len) {
        // A partial frame invalidates the byte stream; never send the next Noise nonce on it.
        close_transport_now();
        return SsErr::FAIL;
    }
    return SsErr::OK;
}

bool SendspinClientConnection::open() {
    WebSocketUrl parsed;
    return parse_url(url_, parsed) && connect_tcp(parsed.host, parsed.port) &&
           upgrade_websocket(parsed.authority, parsed.target);
}

bool SendspinClientConnection::wait_for_connection(int fd) {
    const int64_t deadline = platform_time_us() + OPEN_TIMEOUT_MS * 1000LL;
    while (!stop_) {
        zsock_pollfd pollfd{fd, ZSOCK_POLLOUT, 0};
        const int result = zsock_poll(&pollfd, 1, RECEIVE_TIMEOUT_MS);
        if (result > 0) {
            int error = 0;
            socklen_t size = sizeof(error);
            return zsock_getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0;
        }
        if (result < 0 || platform_time_us() >= deadline) {
            return false;
        }
    }
    return false;
}

bool SendspinClientConnection::connect_address(const zsock_addrinfo& address) {
    const int fd = zsock_socket(address.ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return false;
    }
    const int flags = zsock_fcntl(fd, ZVFS_F_GETFL, 0);
    bool ready = flags >= 0 && zsock_fcntl(fd, ZVFS_F_SETFL, flags | ZVFS_O_NONBLOCK) == 0;
    if (ready && zsock_connect(fd, address.ai_addr, address.ai_addrlen) < 0) {
        ready = errno == EINPROGRESS && wait_for_connection(fd);
    }
    if (ready && !stop_ && zsock_fcntl(fd, ZVFS_F_SETFL, flags) == 0) {
        tcp_socket_ = fd;
        return true;
    }
    zsock_close(fd);
    return false;
}

bool SendspinClientConnection::connect_tcp(const std::string& host, const std::string& port) {
    zsock_addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    zsock_addrinfo* addresses = nullptr;
    if (zsock_getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses) != 0) {
        return false;
    }
    for (auto* addr = addresses; addr != nullptr && !stop_; addr = addr->ai_next) {
        if (connect_address(*addr)) {
            break;
        }
    }
    zsock_freeaddrinfo(addresses);
    return tcp_socket_ >= 0 && !stop_;
}

bool SendspinClientConnection::upgrade_websocket(const std::string& authority,
                                                 const std::string& target) {
    websocket_request request{};
    request.host = authority.c_str();
    request.url = target.c_str();
    request.tmp_buf = http_buffer_;
    request.tmp_buf_len = sizeof(http_buffer_);
    ws_socket_ = websocket_connect(tcp_socket_, &request, OPEN_TIMEOUT_MS, nullptr);
    return ws_socket_ >= 0 && !stop_;
}

// Kept on the receive thread's stack. Control frames leave data-message assembly intact.
struct SendspinClientConnection::ReceiveState {
    uint8_t ping[125];
    size_t ping_size{0};
    bool assembling{false};
    bool frame_start{true};
    bool single_frame{false};
    InboundTarget single_target;
    size_t offset{0};
};

bool SendspinClientConnection::receive_control_frame(ReceiveState& state, const uint8_t* data,
                                                     size_t len, uint32_t type,
                                                     uint64_t remaining) {
    if ((type & WEBSOCKET_FLAG_FINAL) == 0 ||
        state.ping_size + len + remaining > sizeof(state.ping)) {
        return false;
    }
    std::memcpy(state.ping + state.ping_size, data, len);
    state.ping_size += len;
    if (remaining == 0) {
        if ((type & WEBSOCKET_FLAG_PING) != 0 &&
            send_frame(state.ping, state.ping_size, WEBSOCKET_OPCODE_PONG) != SsErr::OK) {
            return false;
        }
        state.ping_size = 0;
    }
    return true;
}

bool SendspinClientConnection::receive_single_frame(ReceiveState& state, const uint8_t* data,
                                                    size_t len, bool frame_end) {
    if (state.single_target.route == InboundRoute::CLOSE) {
        return false;
    }
    if (state.single_target.route == InboundRoute::RECEIVE) {
        if (len > 0) {
            std::memcpy(state.single_target.data + state.offset, data, len);
        }
        state.offset += len;
        if (frame_end) {
            end_inbound_message(true, platform_time_us());
        }
    }
    if (frame_end) {
        state.single_frame = false;
    }
    return true;
}

bool SendspinClientConnection::receive_fragment(const uint8_t* data, size_t len, bool first,
                                                bool text, bool last) {
    const auto destination = begin_inbound_fragment(len, first, text);
    if (destination.route == InboundRoute::CLOSE) {
        return false;
    }
    if (destination.route == InboundRoute::RECEIVE) {
        if (len > 0) {
            std::memcpy(destination.data, data, len);
        }
        end_inbound_fragment(len, last, platform_time_us());
    } else {
        end_inbound_fragment(0, last, platform_time_us());
    }
    return true;
}

bool SendspinClientConnection::receive_data_frame(ReceiveState& state, const uint8_t* data,
                                                  size_t len, uint32_t type,
                                                  uint64_t remaining) {
    const bool frame_end = remaining == 0;
    const bool data_start = (type & (WEBSOCKET_FLAG_TEXT | WEBSOCKET_FLAG_BINARY)) != 0;
    if (state.frame_start && data_start == state.assembling) {
        return false;
    }
    const bool text = (type & WEBSOCKET_FLAG_TEXT) != 0;
    const bool last = frame_end && (type & WEBSOCKET_FLAG_FINAL) != 0;
    if (state.frame_start && data_start && (type & WEBSOCKET_FLAG_FINAL) != 0) {
        // The normal path: reserve the whole message once and copy each socket read
        // directly into the ring, without reallocating a message assembly buffer.
        if (remaining > SIZE_MAX - len) {
            return false;
        }
        state.single_frame = true;
        state.offset = 0;
        state.single_target = begin_inbound_message(len + remaining, text);
    }
    const bool received = state.single_frame
                              ? receive_single_frame(state, data, len, frame_end)
                              : receive_fragment(data, len, !state.assembling, text, last);
    if (!received) {
        return false;
    }
    state.assembling = !last;
    state.frame_start = frame_end;
    return true;
}

void SendspinClientConnection::receive() {
    uint8_t buffer[1024];
    ReceiveState state;
    while (!stop_) {
        uint32_t type = 0;
        uint64_t remaining = 0;
        const int len = websocket_recv_msg(ws_socket_, buffer, sizeof(buffer), &type, &remaining,
                                           RECEIVE_TIMEOUT_MS);
        if (len == -EAGAIN) {
            continue;
        }
        if (len < 0 || (type & WEBSOCKET_FLAG_CLOSE) != 0) {
            break;
        }
        const bool control = (type & (WEBSOCKET_FLAG_PING | WEBSOCKET_FLAG_PONG)) != 0;
        const bool received = control
                                  ? receive_control_frame(state, buffer, len, type, remaining)
                                  : receive_data_frame(state, buffer, len, type, remaining);
        if (!received) {
            break;
        }
    }
    abandon_inbound_message();
}

void SendspinClientConnection::close_sockets() {
    std::lock_guard<PlatformMutex> lock(send_mutex_);
    if (tcp_socket_ >= 0) {
        zsock_shutdown(tcp_socket_, ZSOCK_SHUT_RDWR);
    }
    if (ws_socket_ >= 0) {
        // disconnect releases the WebSocket context; the TCP descriptor remains ours.
        websocket_disconnect(ws_socket_);
        ws_socket_ = -1;
    }
    if (tcp_socket_ >= 0) {
        zsock_close(tcp_socket_);
        tcp_socket_ = -1;
    }
}

void SendspinClientConnection::run() {
    if (open()) {
        connected_ = true;
        mark_ws_upgraded();
        wake_protocol_task();
        if (!inbound_gate_.is_detached()) {
            receive();
        }
    } else {
        SS_LOGW(TAG, "WebSocket connection failed: %s", url_.c_str());
    }
    close_transport_now();
    close_sockets();
    notify_transport_closed();
}

}  // namespace sendspin
