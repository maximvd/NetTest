/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "dhcp_probe.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>

#ifdef __linux__
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace platform::net {
namespace {

// BOOTP (RFC 2131) field offsets.
constexpr size_t kOp = 0;
constexpr size_t kXid = 4;
constexpr size_t kYiaddr = 16;
constexpr size_t kCookie = 236;
constexpr size_t kOptions = 240;
constexpr uint32_t kMagicCookie = 0x63825363;

constexpr uint8_t kBootReply = 2;
constexpr uint8_t kOptionPad = 0;
constexpr uint8_t kOptionRouter = 3;
constexpr uint8_t kOptionLeaseTime = 51;
constexpr uint8_t kOptionMessageType = 53;
constexpr uint8_t kOptionServerId = 54;
constexpr uint8_t kOptionEnd = 255;
constexpr uint8_t kDhcpOffer = 2;

uint32_t read_u32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | data[3];
}

std::string format_ipv4(const uint8_t* data) {
    return std::to_string(data[0]) + "." + std::to_string(data[1]) + "." + std::to_string(data[2]) + "." +
           std::to_string(data[3]);
}

} // namespace

bool parse_dhcp_offer(const uint8_t* data, size_t length, uint32_t xid, DhcpOffer& offer) {
    if (length < kOptions || data[kOp] != kBootReply || read_u32(data + kXid) != xid ||
        read_u32(data + kCookie) != kMagicCookie) {
        return false;
    }

    offer = DhcpOffer{};
    offer.offered_ip = format_ipv4(data + kYiaddr);
    bool is_offer = false;
    for (size_t i = kOptions; i < length;) {
        const uint8_t code = data[i];
        if (code == kOptionPad) {
            ++i;
            continue;
        }
        if (code == kOptionEnd || i + 1 >= length) {
            break;
        }
        const size_t option_length = data[i + 1];
        const uint8_t* value = data + i + 2;
        if (i + 2 + option_length > length) {
            break;
        }
        if (code == kOptionMessageType && option_length >= 1) {
            is_offer = value[0] == kDhcpOffer;
        }
        else if (code == kOptionServerId && option_length >= 4) {
            offer.server = format_ipv4(value);
        }
        else if (code == kOptionRouter && option_length >= 4) {
            offer.router = format_ipv4(value);
        }
        else if (code == kOptionLeaseTime && option_length >= 4) {
            offer.lease_seconds = static_cast<int32_t>(std::min<uint32_t>(read_u32(value), INT32_MAX));
        }
        i += 2 + option_length;
    }
    return is_offer;
}

#ifdef __linux__

namespace {

// Only needed to build the request.
constexpr size_t kFlags = 10;
constexpr size_t kChaddr = 28;
constexpr size_t kBootpMinSize = 300; // some servers ignore shorter requests
constexpr uint8_t kBootRequest = 1;
constexpr uint8_t kOptionParameterList = 55;
constexpr uint8_t kDhcpDiscover = 1;

uint16_t ip_checksum(const uint8_t* data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < length; i += 2) {
        sum += static_cast<uint32_t>((data[i] << 8) | data[i + 1]);
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

void write_u16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value >> 8);
    data[1] = static_cast<uint8_t>(value);
}

void write_u32(uint8_t* data, uint32_t value) {
    write_u16(data, static_cast<uint16_t>(value >> 16));
    write_u16(data + 2, static_cast<uint16_t>(value));
}

class Fd {
public:
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }

private:
    int fd_;
};

// IPv4 + UDP + BOOTP DHCPDISCOVER from 0.0.0.0:68 to 255.255.255.255:67, asking
// servers to broadcast the reply (we have no address to receive a unicast on).
std::vector<uint8_t> build_discover(const std::array<uint8_t, 6>& mac, uint32_t xid) {
    std::vector<uint8_t> bootp(kBootpMinSize, 0);
    bootp[kOp] = kBootRequest;
    bootp[1] = 1; // Ethernet
    bootp[2] = 6; // MAC length
    write_u32(bootp.data() + kXid, xid);
    write_u16(bootp.data() + kFlags, 0x8000); // broadcast
    std::copy(mac.begin(), mac.end(), bootp.begin() + kChaddr);
    write_u32(bootp.data() + kCookie, kMagicCookie);
    const std::array<uint8_t, 12> options = {
        kOptionMessageType, 1, kDhcpDiscover,
        kOptionParameterList, 4, 1 /* subnet mask */, kOptionRouter, 6 /* DNS */, kOptionLeaseTime,
        kOptionEnd, 0, 0,
    };
    std::copy(options.begin(), options.end(), bootp.begin() + kOptions);

    constexpr size_t kIpHeader = 20;
    constexpr size_t kUdpHeader = 8;
    std::vector<uint8_t> packet(kIpHeader + kUdpHeader + bootp.size(), 0);
    uint8_t* ip = packet.data();
    ip[0] = 0x45;                                         // IPv4, 20-byte header
    write_u16(ip + 2, static_cast<uint16_t>(packet.size())); // total length
    ip[8] = 64;                                           // TTL
    ip[9] = IPPROTO_UDP;
    write_u32(ip + 16, 0xffffffff);                       // destination 255.255.255.255
    write_u16(ip + 10, ip_checksum(ip, kIpHeader));
    uint8_t* udp = ip + kIpHeader;
    write_u16(udp, 68);
    write_u16(udp + 2, 67);
    write_u16(udp + 4, static_cast<uint16_t>(kUdpHeader + bootp.size())); // checksum 0: optional over IPv4
    std::copy(bootp.begin(), bootp.end(), udp + kUdpHeader);
    return packet;
}

} // namespace

DhcpProbeResult discover_dhcp_servers(const std::string& interface_name, int wait_ms) {
    DhcpProbeResult result;
    const unsigned int ifindex = if_nametoindex(interface_name.c_str());
    if (ifindex == 0) {
        result.error = "no interface";
        return result;
    }

    std::array<uint8_t, 6> mac{};
    {
        Fd query(::socket(AF_INET, SOCK_DGRAM, 0));
        ifreq request{};
        std::strncpy(request.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);
        if (query.get() < 0 || ::ioctl(query.get(), SIOCGIFHWADDR, &request) != 0) {
            result.error = "no MAC";
            return result;
        }
        std::memcpy(mac.data(), request.ifr_hwaddr.sa_data, mac.size());
    }

    // A cooked packet socket: we write the IP header ourselves, the kernel adds
    // the Ethernet header. Needs CAP_NET_RAW but no address or port 68 of our own.
    Fd fd(::socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP)));
    if (fd.get() < 0) {
        result.error = errno == EPERM ? "no permission" : "socket failed";
        return result;
    }
    sockaddr_ll bind_address{};
    bind_address.sll_family = AF_PACKET;
    bind_address.sll_protocol = htons(ETH_P_IP);
    bind_address.sll_ifindex = static_cast<int>(ifindex);
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&bind_address), sizeof(bind_address)) != 0) {
        result.error = "bind failed";
        return result;
    }

    std::random_device random;
    const uint32_t xid = random();
    const auto packet = build_discover(mac, xid);
    sockaddr_ll destination = bind_address;
    destination.sll_halen = 6;
    std::fill(std::begin(destination.sll_addr), std::begin(destination.sll_addr) + 6, 0xff);
    if (::sendto(fd.get(), packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&destination),
                 sizeof(destination)) < 0) {
        result.error = "send failed";
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    std::array<uint8_t, 2048> buffer{};
    while (true) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (remaining <= 0) {
            break;
        }
        pollfd waiter{fd.get(), POLLIN, 0};
        if (::poll(&waiter, 1, static_cast<int>(remaining)) <= 0) {
            continue; // timeout ends the loop above; EINTR retries
        }
        const auto received = ::recv(fd.get(), buffer.data(), buffer.size(), 0);
        if (received < 28) {
            continue;
        }
        // IPv4 header, then UDP to port 68.
        const uint8_t* ip = buffer.data();
        const size_t ip_header = static_cast<size_t>(ip[0] & 0x0f) * 4;
        if ((ip[0] >> 4) != 4 || ip[9] != IPPROTO_UDP || static_cast<size_t>(received) < ip_header + 8) {
            continue;
        }
        const uint8_t* udp = ip + ip_header;
        if (((udp[2] << 8) | udp[3]) != 68) {
            continue;
        }
        DhcpOffer offer;
        if (!parse_dhcp_offer(udp + 8, static_cast<size_t>(received) - ip_header - 8, xid, offer)) {
            continue;
        }
        if (offer.server.empty()) {
            offer.server = std::to_string(ip[12]) + "." + std::to_string(ip[13]) + "." + std::to_string(ip[14]) +
                           "." + std::to_string(ip[15]);
        }
        const bool known = std::any_of(result.offers.begin(), result.offers.end(), [&](const DhcpOffer& existing) {
            return existing.server == offer.server;
        });
        if (!known) {
            result.offers.push_back(std::move(offer));
        }
    }
    return result;
}

#else

DhcpProbeResult discover_dhcp_servers(const std::string&, int) {
    return {"device only", {}};
}

#endif

} // namespace platform::net
