// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>

#include <sendspin/client.h>
#include <sendspin/metadata_role.h>

#include <cstdio>

class Network final : public sendspin::SendspinNetworkProvider {
public:
    bool is_network_ready() override {
        auto* iface = net_if_get_default();
        return iface != nullptr && net_if_is_up(iface) &&
               net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) != nullptr;
    }
};

class Listener final : public sendspin::SendspinClientListener {
public:
    void on_display_pairing_code(const std::string& code,
                                 sendspin::SendspinPairingCodeFormat) override {
        std::printf("Sendspin pairing code: %s\n", code.c_str());
    }
};

int main() {
    Network network;
    Listener listener;
    sendspin::SendspinClientConfig config;
    config.name = "Zephyr Sendspin";
    config.pairing_code_out_channels = {sendspin::SendspinPairingCodeChannel::DISPLAY};
    config.pairing_code_formats = {sendspin::SendspinPairingCodeFormat::DIGITS};
    sendspin::SendspinClient client(config);
    client.set_network_provider(&network);
    client.set_listener(&listener);
    client.add_metadata();
    client.add_controller();
    // This minimal example keeps its identity and pairing records only for this boot.
    // A product should supply SendspinPersistenceProvider backed by Zephyr settings/NVS.
    if (!client.start()) {
        std::printf("Sendspin startup failed\n");
        return 0;
    }
    bool connecting = false;
    for (;;) {
        client.loop();
        const bool ready = network.is_network_ready();
        if (ready && !connecting) {
            client.connect_to(CONFIG_SENDSPIN_SAMPLE_SERVER_URL);
            connecting = true;
        }
        if (!ready) {
            connecting = false;
        }
        k_sleep(K_MSEC(10));
    }
}
