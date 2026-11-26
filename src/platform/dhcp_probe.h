/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform::net {

struct DhcpOffer {
    std::string server;         // DHCP server identifier (option 54), else the sender's address
    std::string offered_ip;
    std::string router;         // first router (option 3)
    int32_t lease_seconds{-1};  // option 51
};

struct DhcpProbeResult {
    std::string error;          // set when the probe could not run
    std::vector<DhcpOffer> offers; // one per server that answered
};

// Broadcasts one DHCPDISCOVER on the interface and collects every DHCPOFFER
// for `wait_ms`. It never sends a REQUEST, so no address is taken; it shows how
// many DHCP servers are on the segment (more than one points at a rogue or
// misconfigured server). Linux only, needs CAP_NET_RAW.
DhcpProbeResult discover_dhcp_servers(const std::string& interface_name, int wait_ms);

// Parses a DHCPOFFER from a BOOTP payload (UDP data, starting at the op field)
// for transaction `xid`. Exposed for tests.
bool parse_dhcp_offer(const uint8_t* data, size_t length, uint32_t xid, DhcpOffer& offer);

} // namespace platform::net
