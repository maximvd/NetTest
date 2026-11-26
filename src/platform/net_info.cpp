/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "net_info.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/if_packet.h>
#include <linux/wireless.h>
#include <sys/ioctl.h>
#elif defined(__APPLE__)
#include <net/if_dl.h>
#endif

#ifndef APP_NETWORK_CLASS_ROOT
#define APP_NETWORK_CLASS_ROOT "/sys/class/net"
#endif

#ifndef APP_WIRELESS_STATUS_PATH
#define APP_WIRELESS_STATUS_PATH "/proc/net/wireless"
#endif

#ifndef APP_ROUTE_TABLE_PATH
#define APP_ROUTE_TABLE_PATH "/proc/net/route"
#endif

namespace platform::net {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string format_mac(const uint8_t* bytes, size_t length) {
    if (length != 6) {
        return {};
    }
    std::array<char, 18> text{};
    std::snprintf(text.data(),
                  text.size(),
                  "%02x:%02x:%02x:%02x:%02x:%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    return text.data();
}

int32_t prefix_from_netmask(const sockaddr* netmask) {
    if (!netmask || netmask->sa_family != AF_INET) {
        return -1;
    }
    uint32_t mask = ntohl(reinterpret_cast<const sockaddr_in*>(netmask)->sin_addr.s_addr);
    int32_t bits = 0;
    while (mask & 0x80000000U) {
        ++bits;
        mask <<= 1;
    }
    return bits;
}

bool is_link_local_v6(const in6_addr& address) {
    return address.s6_addr[0] == 0xfe && (address.s6_addr[1] & 0xc0) == 0x80;
}

void read_resolv_conf(const char* path, std::vector<std::string>& servers) {
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream words(line);
        std::string keyword;
        std::string address;
        if (words >> keyword >> address && keyword == "nameserver") {
            if (std::find(servers.begin(), servers.end(), address) == servers.end()) {
                servers.push_back(address);
            }
        }
    }
}

std::vector<std::string> read_dns_servers() {
    std::vector<std::string> servers;
    read_resolv_conf("/etc/resolv.conf", servers);

    // systemd-resolved points /etc/resolv.conf at its local stub; the real
    // upstream servers live in a second file.
    const bool only_stub = !servers.empty() &&
                           std::all_of(servers.begin(), servers.end(), [](const std::string& server) {
                               return server.rfind("127.", 0) == 0;
                           });
    if (only_stub) {
        std::vector<std::string> upstream;
        read_resolv_conf("/run/systemd/resolve/resolv.conf", upstream);
        if (!upstream.empty()) {
            return upstream;
        }
    }
    return servers;
}

#if defined(__linux__)

int32_t read_int_file(const std::filesystem::path& path, int32_t fallback = -1) {
    std::ifstream file(path);
    int32_t value = fallback;
    return file >> value ? value : fallback;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::string value;
    std::getline(file, value);
    return trim(value);
}

void read_sysfs(InterfaceInfo& info) {
    const auto root = std::filesystem::path(APP_NETWORK_CLASS_ROOT) / info.name;
    std::error_code ec;
    info.wireless = std::filesystem::exists(root / "wireless", ec) ||
                    std::filesystem::exists(root / "phy80211", ec);
    info.carrier = read_int_file(root / "carrier", 0) == 1;
    info.mtu = read_int_file(root / "mtu", info.mtu);
    info.carrier_down_count = read_int_file(root / "carrier_down_count", -1);
    if (info.mac.empty()) {
        info.mac = read_text_file(root / "address");
    }
    if (!info.wireless && info.carrier) {
        // speed reads EINVAL while the link is down, and reports -1 or a huge
        // value on virtual NICs.
        const auto speed = read_int_file(root / "speed", -1);
        info.speed_mbps = speed > 0 && speed < 1000000 ? speed : -1;
        const auto duplex = read_text_file(root / "duplex");
        if (duplex == "full" || duplex == "half") {
            info.duplex = duplex;
        }
    }
}

void read_wireless(InterfaceInfo& info) {
    std::ifstream file(APP_WIRELESS_STATUS_PATH);
    std::string line;
    while (std::getline(file, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos || trim(line.substr(0, colon)) != info.name) {
            continue;
        }
        // "wlan0: 0000   54.  -56.  -256 ..." -> status, link quality, level (dBm), noise
        std::istringstream values(line.substr(colon + 1));
        std::string status;
        double quality = 0.0;
        double level = 0.0;
        if (values >> status >> quality >> level) {
            auto dbm = static_cast<int32_t>(level);
            if (dbm > 0) {
                dbm -= 256; // older drivers report an unsigned 8-bit value
            }
            if (dbm < 0) {
                info.signal_dbm = dbm;
                info.has_signal = true;
            }
        }
        break;
    }

    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return;
    }
    std::array<char, IW_ESSID_MAX_SIZE + 1> essid{};
    iwreq request{};
    std::strncpy(request.ifr_name, info.name.c_str(), IFNAMSIZ - 1);
    request.u.essid.pointer = essid.data();
    request.u.essid.length = IW_ESSID_MAX_SIZE;
    if (::ioctl(fd, SIOCGIWESSID, &request) == 0) {
        const auto length = std::min<size_t>(request.u.essid.length, IW_ESSID_MAX_SIZE);
        info.ssid.assign(essid.data(), length);
    }
    ::close(fd);
}

void read_default_gateway(NetSnapshot& snapshot) {
    std::ifstream file(APP_ROUTE_TABLE_PATH);
    std::string line;
    std::getline(file, line); // header
    uint32_t best_metric = UINT32_MAX;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string iface;
        std::string destination;
        std::string gateway;
        std::string flags;
        std::string refcnt;
        std::string use;
        uint32_t metric = 0;
        if (!(fields >> iface >> destination >> gateway >> flags >> refcnt >> use >> metric)) {
            continue;
        }
        constexpr unsigned long kRouteUp = 0x1;
        constexpr unsigned long kRouteGateway = 0x2;
        const auto flag_bits = std::strtoul(flags.c_str(), nullptr, 16);
        if (destination != "00000000" || (flag_bits & (kRouteUp | kRouteGateway)) != (kRouteUp | kRouteGateway) ||
            metric >= best_metric) {
            continue;
        }
        // The kernel prints the raw network-order u32 in host byte order.
        in_addr address{};
        address.s_addr = static_cast<in_addr_t>(std::strtoul(gateway.c_str(), nullptr, 16));
        std::array<char, INET_ADDRSTRLEN> text{};
        if (::inet_ntop(AF_INET, &address, text.data(), text.size())) {
            snapshot.gateway = text.data();
            snapshot.gateway_interface = iface;
            best_metric = metric;
        }
    }
}

#elif defined(__APPLE__)

void read_default_gateway(NetSnapshot& snapshot) {
    // Desktop simulator only: good enough to exercise the UI with real data.
    FILE* pipe = ::popen("/sbin/route -n get default 2>/dev/null", "r");
    if (!pipe) {
        return;
    }
    std::array<char, 256> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        const std::string line = trim(buffer.data());
        if (line.rfind("gateway:", 0) == 0) {
            snapshot.gateway = trim(line.substr(8));
        }
        else if (line.rfind("interface:", 0) == 0) {
            snapshot.gateway_interface = trim(line.substr(10));
        }
    }
    ::pclose(pipe);
}

#else

void read_default_gateway(NetSnapshot&) {}

#endif

} // namespace

NetSnapshot read_snapshot() {
    NetSnapshot snapshot;
    std::map<std::string, InterfaceInfo> interfaces;

    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) == 0) {
        for (auto* entry = list; entry; entry = entry->ifa_next) {
            if (!entry->ifa_name || (entry->ifa_flags & IFF_LOOPBACK)) {
                continue;
            }
            auto& info = interfaces[entry->ifa_name];
            info.name = entry->ifa_name;
            info.up = (entry->ifa_flags & IFF_UP) != 0;
            info.carrier = info.carrier || (entry->ifa_flags & IFF_RUNNING) != 0;
            if (!entry->ifa_addr) {
                continue;
            }

            const auto family = entry->ifa_addr->sa_family;
            if (family == AF_INET && info.ipv4.empty()) {
                std::array<char, INET_ADDRSTRLEN> text{};
                const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
                if (::inet_ntop(AF_INET, &address->sin_addr, text.data(), text.size())) {
                    info.ipv4 = text.data();
                    info.prefix_length = prefix_from_netmask(entry->ifa_netmask);
                }
            }
            else if (family == AF_INET6) {
                const auto* address = reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr);
                const bool link_local = is_link_local_v6(address->sin6_addr);
                // Keep the first address, but let a global one replace a link-local one.
                const bool have_global = !info.ipv6.empty() && !info.ipv6_link_local;
                if (!have_global && (info.ipv6.empty() || !link_local)) {
                    std::array<char, INET6_ADDRSTRLEN> text{};
                    if (::inet_ntop(AF_INET6, &address->sin6_addr, text.data(), text.size())) {
                        info.ipv6 = text.data();
                        info.ipv6_link_local = link_local;
                    }
                }
            }
#if defined(__linux__)
            else if (family == AF_PACKET) {
                const auto* link = reinterpret_cast<const sockaddr_ll*>(entry->ifa_addr);
                info.mac = format_mac(link->sll_addr, link->sll_halen);
            }
#elif defined(__APPLE__)
            else if (family == AF_LINK) {
                const auto* link = reinterpret_cast<const sockaddr_dl*>(entry->ifa_addr);
                info.mac = format_mac(reinterpret_cast<const uint8_t*>(LLADDR(link)), link->sdl_alen);
            }
#endif
        }
        ::freeifaddrs(list);
    }

    for (auto& [name, info] : interfaces) {
#if defined(__linux__)
        read_sysfs(info);
        if (info.wireless) {
            read_wireless(info);
        }
        snapshot.interfaces.push_back(std::move(info));
#else
        // macOS lists many virtual interfaces (utun, awdl, llw, bridge...);
        // only show the ones that actually carry IPv4.
        if (!info.ipv4.empty()) {
            snapshot.interfaces.push_back(std::move(info));
        }
#endif
    }

    read_default_gateway(snapshot);
    snapshot.dns_servers = read_dns_servers();
    return snapshot;
}

int32_t signal_quality_percent(int32_t signal_dbm) {
    // Linear map of -90 dBm (unusable) .. -30 dBm (excellent).
    return std::clamp((signal_dbm + 90) * 100 / 60, 0, 100);
}

} // namespace platform::net
