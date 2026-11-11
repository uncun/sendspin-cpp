// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <zephyr/net/websocket.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace sendspin {
// Zephyr 4.3/4.4 can return a partial payload on timeout without updating type/remaining.
// A one-byte first read always fills its payload buffer (or ends an empty frame), so its
// metadata is reliable. Subsequent reads stay within that frame and count its bytes locally.
// No Zephyr internals or patches are needed, and idle waits remain bounded for cancellation.
class WebSocketReader {
public:
    int receive(int socket, uint8_t* buffer, size_t capacity, uint32_t* type, uint64_t* remaining,
                int32_t timeout_ms) {
        const bool first = remaining_ == 0;
        const size_t count = first ? 1 : std::min(uint64_t(capacity), remaining_);
        const int result = websocket_recv_msg(socket, buffer, count, &type_, remaining, timeout_ms);
        if (result < 0) {
            return result;
        }
        if (first) {
            remaining_ = *remaining;
        } else {
            remaining_ -= static_cast<size_t>(result);
        }
        *type = type_;
        *remaining = remaining_;
        return result;
    }

private:
    uint32_t type_{0};
    uint64_t remaining_{0};
};
}  // namespace sendspin
