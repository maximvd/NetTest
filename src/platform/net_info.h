/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform::net {

struct InterfaceInfo {
    std::string name;
    std::string mac;           // "aa:bb:cc:dd:ee:ff", empty when unknown
    std::string ipv4;          // first IPv4 address, empty when none
    int32_t prefix_length{-1}; // IPv4 prefix length, -1 when unknown
    std::string ipv6;          // first global (or link-local) IPv6 address
    bool ipv6_link_local{false};
    bool up{false};            // administratively up (IFF_UP)
    bool carrier{false};       // link detected (IFF_RUNNING / carrier)
    bool wireless{false};
    int32_t speed_mbps{-1};    // wired only, -1 when unknown
    std::string duplex;        // "full", "half" or empty
    int32_t mtu{-1};
    int32_t carrier_down_count{-1}; // times the link dropped since boot (Linux), -1 when unknown
    std::string ssid;          // wireless only
    bool has_signal{false};
    int32_t signal_dbm{0};     // wireless only, valid when has_signal
};

struct NetSnapshot {
    std::vector<InterfaceInfo> interfaces; // loopback excluded, sorted by name
    std::string gateway;                   // IPv4 default gateway, empty when none
    std::string gateway_interface;
    std::vector<std::string> dns_servers;
};

// Reads the current interface, route and resolver state. Cheap enough to call
// from the UI thread every second or two (sysfs/procfs reads and getifaddrs).
NetSnapshot read_snapshot();

// Converts a Wi-Fi RSSI in dBm into a rough 0-100 quality percentage.
int32_t signal_quality_percent(int32_t signal_dbm);

} // namespace platform::net
