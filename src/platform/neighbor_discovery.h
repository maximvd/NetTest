/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace platform::net {

// What the switch on the other end of the cable says about itself, learned
// from LLDP (IEEE 802.1AB) or Cisco CDP advertisements.
struct Neighbor {
    enum class Protocol { Lldp, Cdp };

    Protocol protocol{Protocol::Lldp};
    std::string system_name;   // switch name (LLDP System Name / CDP Device ID)
    std::string port_id;       // e.g. "Gi1/0/12" (LLDP Port ID / CDP Port ID)
    std::string port_description;
    std::string chassis_id;
    std::string management_ip;
    std::string platform;      // CDP platform / first line of LLDP System Description
    int32_t vlan{-1};          // native / port VLAN id, -1 when not advertised
    std::string vlan_name;     // LLDP 802.1 VLAN Name for that VLAN, if advertised
    int32_t voice_vlan{-1};    // LLDP-MED network policy (voice) / CDP appliance VLAN, -1 when not advertised
    int32_t poe_class{-1};     // PoE power class 0-4 (802.3 Power via MDI), -1 when not advertised
    int32_t poe_milliwatts{-1}; // power the switch allots this port, -1 when not advertised
    int32_t ttl_seconds{-1};
};

// Parse a full Ethernet frame (starting at the destination MAC). Return
// nullopt when the frame is not a well-formed LLDP / CDP advertisement.
std::optional<Neighbor> parse_lldp_frame(const uint8_t* frame, size_t length);
std::optional<Neighbor> parse_cdp_frame(const uint8_t* frame, size_t length);

// Listens passively on one wired interface for LLDP and CDP frames on a
// background thread. Needs CAP_NET_RAW (the device service runs as root).
// Switches advertise every 30 s (LLDP) / 60 s (CDP) by default, so the first
// result can take that long to arrive.
class NeighborListener {
public:
    enum class State {
        Idle,        // not started
        Listening,   // socket open, nothing heard yet
        Found,       // at least one advertisement received
        Unsupported, // not available on this platform (desktop simulator)
        Error,       // socket setup failed, see error()
    };

    struct Status {
        State state{State::Idle};
        std::string interface_name;
        std::string error;
        std::optional<Neighbor> neighbor;
        int32_t seconds_since_update{-1}; // since last frame, or since start while listening
    };

    NeighborListener() = default;
    ~NeighborListener();

    NeighborListener(const NeighborListener&) = delete;
    NeighborListener& operator=(const NeighborListener&) = delete;

    // Starts listening on the interface, stopping any previous listener first.
    // Calling it again with the interface already being listened on does nothing.
    void start(const std::string& interface_name);
    void stop();

    Status status() const;

    // Incremented whenever status() would return something new.
    uint32_t revision() const;

private:
    void run(std::string interface_name);
    void set_error(const std::string& message);
    void set_neighbor(Neighbor neighbor);

    using Clock = std::chrono::steady_clock;

    mutable std::mutex mutex_;
    std::thread thread_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<uint32_t> revision_{0};
    State state_{State::Idle};
    std::string interface_name_;
    std::string error_;
    std::optional<Neighbor> neighbor_;
    Clock::time_point last_update_{};
};

} // namespace platform::net
