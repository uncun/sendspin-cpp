// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#include "platform/network_info.h"

#include <zephyr/net/net_if.h>

#include <cstdio>

namespace sendspin {
std::optional<MacAddressText> platform_get_interface_mac() {
    net_if* iface = net_if_get_default();
    if (iface == nullptr) {
        return std::nullopt;
    }
    const net_linkaddr* address = net_if_get_link_addr(iface);
    if (address == nullptr || address->len != 6) {
        return std::nullopt;
    }
    MacAddressText result{};
    std::snprintf(result.data(), result.size(), "%02x:%02x:%02x:%02x:%02x:%02x", address->addr[0],
                  address->addr[1], address->addr[2], address->addr[3], address->addr[4],
                  address->addr[5]);
    return result;
}
}  // namespace sendspin
