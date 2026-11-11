// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#include "websocket_url.h"
#include <gtest/gtest.h>

using namespace sendspin;

TEST(WebSocketUrl, AcceptsSupportedUrisAndRejectsTheWholeMalformedUri) {
    struct Case {
        std::string_view url;
        bool valid;
        std::string_view host;
        std::string_view port;
        std::string_view target;
    };
    const Case cases[] = {
        // Control: a DNS host, default port and default path are supported.
        {"ws://music.local", true, "music.local", "80", "/"},
        {"ws://127.0.0.1:8927/sendspin?test=1", true, "127.0.0.1", "8927", "/sendspin?test=1"},
        {"ws://[::1]:65535/sendspin", true, "::1", "65535", "/sendspin"},
        {"ws://[2001:db8::1]/", true, "2001:db8::1", "80", "/"},
        {"ws://host?test=1", true, "host", "80", "?test=1"},
        {"ws://host:1/", true, "host", "1", "/"},
        {"wss://host/", false, {}, {}, {}},
        {"http://host/", false, {}, {}, {}},
        {"ws://", false, {}, {}, {}},
        {"ws:///sendspin", false, {}, {}, {}},
        {"ws://user@host/", false, {}, {}, {}},
        {"ws://host/path#fragment", false, {}, {}, {}},
        {"ws://host:", false, {}, {}, {}},
        {"ws://host:0/", false, {}, {}, {}},
        {"ws://host:65536/", false, {}, {}, {}},
        {"ws://host:000080/", false, {}, {}, {}},
        {"ws://host:-1/", false, {}, {}, {}},
        {"ws://host:+80/", false, {}, {}, {}},
        {"ws://host:80x/", false, {}, {}, {}},
        {"ws://:80/", false, {}, {}, {}},
        {"ws://[::1", false, {}, {}, {}},
        {"ws://[]/", false, {}, {}, {}},
        {"ws://[::1]extra/", false, {}, {}, {}},
        {"ws://[::1]:/", false, {}, {}, {}},
        {"ws://::1/", false, {}, {}, {}},
        {"ws://host]/", false, {}, {}, {}},
        {"ws://host/with space", false, {}, {}, {}},
        {"ws://host\r\nInjected:header/", false, {}, {}, {}},
        {std::string_view("ws://host/\0hidden", 17), false, {}, {}, {}},
        {"ws://host/\x7f", false, {}, {}, {}},
    };
    for (const auto& row : cases) {
        SCOPED_TRACE(row.url);
        const auto parsed = parse_websocket_url(row.url);
        ASSERT_EQ(parsed.has_value(), row.valid);
        if (parsed) {
            EXPECT_EQ(parsed->host, row.host);
            EXPECT_EQ(parsed->port, row.port);
            EXPECT_EQ(parsed->target, row.target);
            EXPECT_EQ(parsed->authority.data(), row.url.data() + 5);
            EXPECT_EQ(parsed->host.data(), row.url.data() + 5 + (parsed->authority.front() == '['));
        }
    }
}
