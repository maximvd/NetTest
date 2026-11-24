/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform::net {

struct ProbeResult {
    bool ok{false};
    double ms{0.0};
    std::string detail; // resolved address, or a short error message
};

// All probes block the calling thread; run them off the UI thread.

// One ICMP echo request to an IPv4 address. Uses an unprivileged ICMP
// datagram socket when the kernel allows it, else a raw socket (root).
ProbeResult ping_once(const std::string& ipv4, uint16_t sequence, int timeout_ms);

// Sends one DNS "A" query for `host` straight to `server` (numeric IPv4 or
// IPv6 address, UDP port 53), bypassing the system resolver, so each
// configured server can be checked on its own. ok means the server answered
// NOERROR with at least one record; detail then holds the first IPv4 answer.
ProbeResult query_dns_server(const std::string& server, const std::string& host, int timeout_ms);

// TCP three-way handshake to ipv4:port.
ProbeResult tcp_connect(const std::string& ipv4, uint16_t port, int timeout_ms);

// Runs the gateway / internet / DNS checks one after another on a worker thread.
class ConnectivityTester {
public:
    enum class StepState { Idle, Running, Ok, Warn, Fail, Skipped };

    enum Step : size_t {
        GatewayPing = 0,
        InternetPing,
        Dns,
        InternetTcp,
        StepCount,
    };

    struct StepResult {
        StepState state{StepState::Idle};
        std::string value; // e.g. "4.1 ms  0% loss"
    };

    struct Report {
        std::array<StepResult, StepCount> steps{};
        bool running{false};
        uint32_t runs{0};
    };

    static constexpr const char* kInternetPingTarget = "1.1.1.1";
    static constexpr const char* kDnsTestHost = "example.com";
    static constexpr const char* kTcpTarget = "1.1.1.1";
    static constexpr uint16_t kTcpPort = 443;

    ConnectivityTester() = default;
    ~ConnectivityTester();

    ConnectivityTester(const ConnectivityTester&) = delete;
    ConnectivityTester& operator=(const ConnectivityTester&) = delete;

    // At most this many DNS servers are queried (resolv.conf allows three).
    static constexpr size_t kMaxDnsServers = 3;

    // Starts a run unless one is already in progress. An empty gateway skips
    // the gateway ping; every DNS server (up to kMaxDnsServers) is queried.
    void start(const std::string& gateway, const std::vector<std::string>& dns_servers);

    Report report() const;
    uint32_t revision() const;

    static const char* step_name(Step step);

private:
    void run(std::string gateway, std::vector<std::string> dns_servers);
    StepResult check_dns_servers(const std::vector<std::string>& servers);
    void set_step(Step step, StepState state, std::string value);
    StepResult ping_series(const std::string& target);

    mutable std::mutex mutex_;
    std::thread thread_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<uint32_t> revision_{0};
    Report report_;
};

} // namespace platform::net
