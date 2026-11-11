// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace sendspin {

/// Views into the caller's URL; it must outlive the parsed result. The default port and
/// target view string literals. A query-only target needs a leading slash on the wire.
struct WebSocketUrl {
    std::string_view authority;
    std::string_view host;
    std::string_view port{"80"};
    std::string_view target{"/"};
};

/// RFC 6455 section 3: the outbound Zephyr transport accepts ws URIs, without credentials
/// or fragments. Resolver-specific host validation happens when connecting.
inline std::optional<WebSocketUrl> parse_websocket_url(std::string_view url) {
    if (!url.starts_with("ws://") || url.find('#') != std::string_view::npos) {
        return std::nullopt;
    }
    constexpr unsigned char ASCII_DELETE = 0x7f;
    // Neither an HTTP request line nor its Host header may contain raw whitespace or controls.
    for (const unsigned char ch : url) {
        if (ch <= ' ' || ch == ASCII_DELETE) {
            return std::nullopt;
        }
    }
    const size_t target_pos = url.find_first_of("/?", 5);
    WebSocketUrl parsed;
    parsed.authority = url.substr(
        5, target_pos == std::string_view::npos ? std::string_view::npos : target_pos - 5);
    const auto authority = parsed.authority;
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return std::nullopt;
    }
    if (authority.front() == '[') {
        const size_t end = authority.find(']');
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        parsed.host = authority.substr(1, end - 1);
        if (end + 1 < authority.size()) {
            if (authority[end + 1] != ':') {
                return std::nullopt;
            }
            parsed.port = authority.substr(end + 2);
        }
    } else {
        const size_t colon = authority.find(':');
        parsed.host = authority.substr(0, colon);
        if (colon != std::string_view::npos) {
            parsed.port = authority.substr(colon + 1);
        }
    }
    if (parsed.host.empty() || parsed.host.find_first_of("[]") != std::string_view::npos ||
        parsed.port.empty() || parsed.port.size() > 5) {
        return std::nullopt;
    }
    unsigned port = 0;
    const auto end = parsed.port.data() + parsed.port.size();
    const auto result =
        std::from_chars(parsed.port.data(), parsed.port.data() + parsed.port.size(), port);
    if (result.ec != std::errc{} || result.ptr != end || port == 0 ||
        port > std::numeric_limits<uint16_t>::max()) {
        return std::nullopt;
    }
    if (target_pos != std::string_view::npos) {
        parsed.target = url.substr(target_pos);
    }
    return parsed;
}

}  // namespace sendspin
