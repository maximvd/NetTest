/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "net_probe.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace platform::net {
namespace {

using Clock = std::chrono::steady_clock;

constexpr uint8_t kIcmpEchoReply = 0;
constexpr uint8_t kIcmpEchoRequest = 8;
constexpr size_t kIcmpHeaderSize = 8;
constexpr size_t kIcmpPayloadSize = 32;
constexpr int kPingCount = 4;
constexpr int kPingTimeoutMs = 1000;
constexpr int kPingIntervalMs = 250;
constexpr int kTcpTimeoutMs = 3000;
constexpr int kDnsTimeoutMs = 1500; // per attempt, two attempts

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

uint16_t icmp_checksum(const uint8_t* data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < length; i += 2) {
        sum += static_cast<uint32_t>((data[i] << 8) | data[i + 1]);
    }
    if (length & 1) {
        sum += static_cast<uint32_t>(data[length - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

std::string format_ms(double ms) {
    std::array<char, 24> text{};
    if (ms < 10.0) {
        std::snprintf(text.data(), text.size(), "%.1f ms", ms);
    }
    else {
        std::snprintf(text.data(), text.size(), "%.0f ms", ms);
    }
    return text.data();
}

class SocketGuard {
public:
    explicit SocketGuard(int fd) : fd_(fd) {}
    ~SocketGuard() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;
    int get() const { return fd_; }

private:
    int fd_;
};

} // namespace

ProbeResult ping_once(const std::string& ipv4, uint16_t sequence, int timeout_ms) {
    ProbeResult result;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    if (::inet_pton(AF_INET, ipv4.c_str(), &target.sin_addr) != 1) {
        result.detail = "bad address";
        return result;
    }

    // Unprivileged ICMP sockets: Linux (net.ipv4.ping_group_range) and macOS.
    bool raw = false;
    int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (fd < 0) {
        fd = ::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        raw = true;
    }
    SocketGuard guard(fd);
    if (fd < 0) {
        result.detail = errno == EPERM || errno == EACCES ? "no permission" : std::strerror(errno);
        return result;
    }

    // On Linux datagram sockets the kernel rewrites the identifier, so replies
    // are matched on sequence number (and source address) only.
    const auto identifier = static_cast<uint16_t>(::getpid() & 0xffff);
    std::array<uint8_t, kIcmpHeaderSize + kIcmpPayloadSize> packet{};
    packet[0] = kIcmpEchoRequest;
    packet[4] = static_cast<uint8_t>(identifier >> 8);
    packet[5] = static_cast<uint8_t>(identifier & 0xff);
    packet[6] = static_cast<uint8_t>(sequence >> 8);
    packet[7] = static_cast<uint8_t>(sequence & 0xff);
    for (size_t i = kIcmpHeaderSize; i < packet.size(); ++i) {
        packet[i] = static_cast<uint8_t>(i);
    }
    const auto checksum = icmp_checksum(packet.data(), packet.size());
    packet[2] = static_cast<uint8_t>(checksum >> 8);
    packet[3] = static_cast<uint8_t>(checksum & 0xff);

    const auto start = Clock::now();
    if (::sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&target), sizeof(target)) < 0) {
        result.detail = errno == ENETUNREACH || errno == EHOSTUNREACH ? "unreachable" : std::strerror(errno);
        return result;
    }

    std::array<uint8_t, 1500> buffer{};
    while (true) {
        const int remaining = timeout_ms - static_cast<int>(elapsed_ms(start));
        if (remaining <= 0) {
            result.detail = "timeout";
            return result;
        }
        pollfd poll_fd{fd, POLLIN, 0};
        const int ready = ::poll(&poll_fd, 1, remaining);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            result.detail = "timeout";
            return result;
        }

        sockaddr_in from{};
        socklen_t from_length = sizeof(from);
        const auto received = ::recvfrom(fd, buffer.data(), buffer.size(), 0,
                                         reinterpret_cast<sockaddr*>(&from), &from_length);
        if (received <= 0 || from.sin_addr.s_addr != target.sin_addr.s_addr) {
            continue;
        }

        // Raw sockets (and macOS datagram sockets) include the IPv4 header.
        size_t offset = 0;
        if ((buffer[0] >> 4) == 4) {
            offset = static_cast<size_t>(buffer[0] & 0x0f) * 4;
        }
        if (static_cast<size_t>(received) < offset + kIcmpHeaderSize) {
            continue;
        }
        const uint8_t* icmp = buffer.data() + offset;
        const auto reply_identifier = static_cast<uint16_t>((icmp[4] << 8) | icmp[5]);
        const auto reply_sequence = static_cast<uint16_t>((icmp[6] << 8) | icmp[7]);
        if (icmp[0] != kIcmpEchoReply || reply_sequence != sequence ||
            (raw && reply_identifier != identifier)) {
            continue;
        }
        result.ok = true;
        result.ms = elapsed_ms(start);
        return result;
    }
}

namespace {

// Advances `pos` past a (possibly compressed) DNS name. False when malformed.
bool skip_dns_name(const uint8_t* message, size_t length, size_t& pos) {
    for (int labels = 0; labels < 128; ++labels) {
        if (pos >= length) {
            return false;
        }
        const uint8_t label_length = message[pos];
        if ((label_length & 0xc0) == 0xc0) { // compression pointer ends the name
            pos += 2;
            return pos <= length;
        }
        pos += 1 + label_length;
        if (label_length == 0) {
            return pos <= length;
        }
    }
    return false;
}

const char* dns_rcode_text(uint8_t rcode) {
    switch (rcode) {
        case 1:
            return "FORMERR";
        case 2:
            return "SERVFAIL";
        case 3:
            return "NXDOMAIN";
        case 5:
            return "REFUSED";
        default:
            return "bad reply";
    }
}

} // namespace

ProbeResult query_dns_server(const std::string& server, const std::string& host, int timeout_ms) {
    ProbeResult result;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV; // never recurse into DNS here
    addrinfo* target = nullptr;
    if (::getaddrinfo(server.c_str(), "53", &hints, &target) != 0 || !target) {
        result.detail = "bad address";
        return result;
    }
    SocketGuard guard(::socket(target->ai_family, SOCK_DGRAM, 0));
    const int fd = guard.get();
    const bool connected = fd >= 0 && ::connect(fd, target->ai_addr, target->ai_addrlen) == 0;
    const int connect_error = errno;
    ::freeaddrinfo(target);
    if (!connected) {
        result.detail = connect_error == ENETUNREACH || connect_error == EHOSTUNREACH ? "unreachable"
                                                                                      : std::strerror(connect_error);
        return result;
    }

    // Header: id, flags (RD), QDCOUNT=1; then QNAME, QTYPE=A, QCLASS=IN.
    const auto id = static_cast<uint16_t>(Clock::now().time_since_epoch().count() ^ ::getpid());
    std::vector<uint8_t> query = {static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id & 0xff),
                                  0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    size_t label_start = 0;
    while (label_start <= host.size()) {
        auto dot = host.find('.', label_start);
        if (dot == std::string::npos) {
            dot = host.size();
        }
        const auto label_length = dot - label_start;
        if (label_length == 0 || label_length > 63) {
            break;
        }
        query.push_back(static_cast<uint8_t>(label_length));
        query.insert(query.end(), host.begin() + static_cast<std::ptrdiff_t>(label_start),
                     host.begin() + static_cast<std::ptrdiff_t>(dot));
        label_start = dot + 1;
    }
    query.insert(query.end(), {0x00, 0x00, 0x01, 0x00, 0x01});

    const auto start = Clock::now();
    if (::send(fd, query.data(), query.size(), 0) < 0) {
        result.detail = errno == ENETUNREACH || errno == EHOSTUNREACH ? "unreachable" : std::strerror(errno);
        return result;
    }

    std::array<uint8_t, 1500> reply{};
    while (true) {
        const int remaining = timeout_ms - static_cast<int>(elapsed_ms(start));
        if (remaining <= 0) {
            result.detail = "timeout";
            return result;
        }
        pollfd poll_fd{fd, POLLIN, 0};
        const int ready = ::poll(&poll_fd, 1, remaining);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            result.detail = "timeout";
            return result;
        }
        const auto received = ::recv(fd, reply.data(), reply.size(), 0);
        if (received < 0) {
            // ICMP port unreachable on a connected UDP socket: nothing listens on :53.
            result.detail = errno == ECONNREFUSED ? "refused" : std::strerror(errno);
            return result;
        }
        const auto length = static_cast<size_t>(received);
        if (length < 12 || reply[0] != query[0] || reply[1] != query[1] || !(reply[2] & 0x80)) {
            continue; // not our answer
        }

        result.ms = elapsed_ms(start);
        const uint8_t rcode = reply[3] & 0x0f;
        const uint16_t answers = static_cast<uint16_t>((reply[6] << 8) | reply[7]);
        if (rcode != 0) {
            result.detail = dns_rcode_text(rcode);
            return result;
        }
        if (answers == 0) {
            result.detail = "no answer";
            return result;
        }
        result.ok = true;

        // Skip the echoed question, then look for the first A record.
        size_t pos = 12;
        if (!skip_dns_name(reply.data(), length, pos) || pos + 4 > length) {
            return result;
        }
        pos += 4;
        for (uint16_t i = 0; i < answers; ++i) {
            if (!skip_dns_name(reply.data(), length, pos) || pos + 10 > length) {
                break;
            }
            const uint16_t type = static_cast<uint16_t>((reply[pos] << 8) | reply[pos + 1]);
            const uint16_t data_length = static_cast<uint16_t>((reply[pos + 8] << 8) | reply[pos + 9]);
            pos += 10;
            if (pos + data_length > length) {
                break;
            }
            if (type == 1 && data_length == 4) {
                std::array<char, INET_ADDRSTRLEN> text{};
                if (::inet_ntop(AF_INET, reply.data() + pos, text.data(), text.size())) {
                    result.detail = text.data();
                }
                break;
            }
            pos += data_length;
        }
        return result;
    }
}

ProbeResult tcp_connect(const std::string& ipv4, uint16_t port, int timeout_ms) {
    ProbeResult result;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    if (::inet_pton(AF_INET, ipv4.c_str(), &target.sin_addr) != 1) {
        result.detail = "bad address";
        return result;
    }

    SocketGuard guard(::socket(AF_INET, SOCK_STREAM, 0));
    const int fd = guard.get();
    if (fd < 0) {
        result.detail = std::strerror(errno);
        return result;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    const auto start = Clock::now();
    int error = 0;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&target), sizeof(target)) != 0) {
        if (errno != EINPROGRESS) {
            error = errno;
        }
        else {
            pollfd poll_fd{fd, POLLOUT, 0};
            int ready = 0;
            do {
                const int remaining = timeout_ms - static_cast<int>(elapsed_ms(start));
                ready = remaining > 0 ? ::poll(&poll_fd, 1, remaining) : 0;
            } while (ready < 0 && errno == EINTR);
            if (ready <= 0) {
                result.detail = "timeout";
                return result;
            }
            socklen_t length = sizeof(error);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);
        }
    }
    if (error != 0) {
        result.detail = error == ECONNREFUSED ? "refused"
                        : error == ENETUNREACH || error == EHOSTUNREACH ? "unreachable"
                                                                        : std::strerror(error);
        return result;
    }
    result.ok = true;
    result.ms = elapsed_ms(start);
    return result;
}

ConnectivityTester::~ConnectivityTester() {
    stop_requested_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ConnectivityTester::start(const std::string& gateway, const std::vector<std::string>& dns_servers) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (report_.running) {
            return;
        }
        report_.running = true;
        for (auto& step : report_.steps) {
            step = StepResult{StepState::Idle, ""};
        }
        ++revision_;
    }
    // The previous run has finished (running == false), so this join is immediate.
    if (thread_.joinable()) {
        thread_.join();
    }
    stop_requested_ = false;
    thread_ = std::thread(&ConnectivityTester::run, this, gateway, dns_servers);
}

ConnectivityTester::Report ConnectivityTester::report() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return report_;
}

uint32_t ConnectivityTester::revision() const {
    return revision_.load();
}

const char* ConnectivityTester::step_name(Step step) {
    switch (step) {
        case GatewayPing:
            return "Gateway";
        case InternetPing:
            return "Ping 1.1.1.1";
        case Dns:
            return "DNS";
        case InternetTcp:
            return "Internet";
        default:
            return "";
    }
}

void ConnectivityTester::set_step(Step step, StepState state, std::string value) {
    std::lock_guard<std::mutex> lock(mutex_);
    report_.steps[step] = StepResult{state, std::move(value)};
    ++revision_;
}

ConnectivityTester::StepResult ConnectivityTester::ping_series(const std::string& target) {
    static std::atomic<uint16_t> next_sequence{1};
    int received = 0;
    double total_ms = 0.0;
    std::string last_error;

    for (int i = 0; i < kPingCount && !stop_requested_; ++i) {
        if (i > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kPingIntervalMs));
        }
        const auto probe = ping_once(target, next_sequence++, kPingTimeoutMs);
        if (probe.ok) {
            ++received;
            total_ms += probe.ms;
        }
        else {
            last_error = probe.detail;
            if (last_error == "no permission" || last_error == "unreachable") {
                break; // retrying will not help
            }
        }
    }

    if (received == 0) {
        return {StepState::Fail, last_error.empty() ? "no reply" : last_error};
    }
    const int loss = (kPingCount - received) * 100 / kPingCount;
    std::array<char, 16> loss_text{};
    std::snprintf(loss_text.data(), loss_text.size(), "%d%% loss", loss);
    return {loss > 0 ? StepState::Warn : StepState::Ok,
            format_ms(total_ms / received) + "  " + loss_text.data()};
}

ConnectivityTester::StepResult ConnectivityTester::check_dns_servers(const std::vector<std::string>& servers) {
    if (servers.empty()) {
        return {StepState::Fail, "no DNS servers configured"};
    }

    // One entry per server, e.g. "10.0.1.1 12 ms   8.8.8.8 timeout".
    const size_t count = std::min(servers.size(), kMaxDnsServers);
    size_t answered = 0;
    std::string summary;
    for (size_t i = 0; i < count && !stop_requested_; ++i) {
        auto probe = query_dns_server(servers[i], kDnsTestHost, kDnsTimeoutMs);
        if (!probe.ok && probe.detail == "timeout" && !stop_requested_) {
            // UDP: one lost packet should not mark a server dead (resolvers retry too).
            probe = query_dns_server(servers[i], kDnsTestHost, kDnsTimeoutMs);
        }
        if (!summary.empty()) {
            summary += "   ";
        }
        summary += servers[i] + " " + (probe.ok ? format_ms(probe.ms) : probe.detail);
        if (probe.ok) {
            ++answered;
        }
    }

    const StepState state = answered == count ? StepState::Ok
                            : answered > 0    ? StepState::Warn
                                              : StepState::Fail;
    return {state, summary};
}

void ConnectivityTester::run(std::string gateway, std::vector<std::string> dns_servers) {
    if (gateway.empty()) {
        set_step(GatewayPing, StepState::Skipped, "no default route");
    }
    else {
        set_step(GatewayPing, StepState::Running, "pinging " + gateway);
        auto result = ping_series(gateway);
        set_step(GatewayPing, result.state, std::move(result.value));
    }

    if (!stop_requested_) {
        set_step(InternetPing, StepState::Running, "pinging...");
        auto result = ping_series(kInternetPingTarget);
        set_step(InternetPing, result.state, std::move(result.value));
    }

    if (!stop_requested_) {
        set_step(Dns, StepState::Running, std::string("querying ") + kDnsTestHost);
        auto result = check_dns_servers(dns_servers);
        set_step(Dns, result.state, std::move(result.value));
    }

    if (!stop_requested_) {
        set_step(InternetTcp, StepState::Running, "connecting...");
        const auto probe = tcp_connect(kTcpTarget, kTcpPort, kTcpTimeoutMs);
        set_step(InternetTcp,
                 probe.ok ? StepState::Ok : StepState::Fail,
                 probe.ok ? "online  " + format_ms(probe.ms) : probe.detail);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    report_.running = false;
    ++report_.runs;
    ++revision_;
}

} // namespace platform::net
