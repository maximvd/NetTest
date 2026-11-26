/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "neighbor_discovery.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(__linux__)
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace platform::net {
namespace {

constexpr uint16_t kEtherTypeVlan = 0x8100;
constexpr uint16_t kEtherTypeLldp = 0x88cc;
[[maybe_unused]] constexpr std::array<uint8_t, 6> kLldpMulticast = {0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e};
constexpr std::array<uint8_t, 6> kCdpMulticast = {0x01, 0x00, 0x0c, 0xcc, 0xcc, 0xcc};

uint16_t read_u16(const uint8_t* data) {
    return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

// Keeps printable ASCII only and trims surrounding whitespace / padding NULs.
std::string clean_string(const uint8_t* data, size_t length) {
    std::string text;
    text.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        const auto ch = data[i];
        if (ch >= 0x20 && ch < 0x7f) {
            text.push_back(static_cast<char>(ch));
        }
        else if (ch == '\n' || ch == '\r' || ch == '\t') {
            text.push_back(' ');
        }
    }
    const auto first = text.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(' ');
    return text.substr(first, last - first + 1);
}

// First line of a multi-line description ("Cisco IOS Software, C2960...").
std::string first_line(const uint8_t* data, size_t length) {
    size_t end = 0;
    while (end < length && data[end] != '\n' && data[end] != '\r') {
        ++end;
    }
    return clean_string(data, end);
}

std::string format_mac(const uint8_t* bytes) {
    std::array<char, 18> text{};
    std::snprintf(text.data(),
                  text.size(),
                  "%02x:%02x:%02x:%02x:%02x:%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    return text.data();
}

std::string format_ipv4(const uint8_t* bytes) {
    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    return text.data();
}

// Decodes the LLDP Chassis ID / Port ID value given its subtype.
std::string decode_lldp_id(uint8_t subtype, const uint8_t* value, size_t length, bool is_port) {
    const uint8_t mac_subtype = is_port ? 3 : 4;
    const uint8_t address_subtype = is_port ? 4 : 5;
    if (subtype == mac_subtype && length == 6) {
        return format_mac(value);
    }
    if (subtype == address_subtype && length == 5 && value[0] == 1) { // IANA family 1 = IPv4
        return format_ipv4(value + 1);
    }
    return clean_string(value, length);
}

// Returns the offset of the payload after the Ethernet (and optional 802.1Q)
// header, and the EtherType / 802.3 length field found there.
bool skip_ethernet_header(const uint8_t* frame, size_t length, size_t& offset, uint16_t& type_or_length) {
    if (length < 14) {
        return false;
    }
    offset = 12;
    type_or_length = read_u16(frame + offset);
    if (type_or_length == kEtherTypeVlan) {
        if (length < 18) {
            return false;
        }
        offset += 4;
        type_or_length = read_u16(frame + offset);
    }
    offset += 2;
    return true;
}

// CDP address list: count(4) then {proto type(1), proto len(1), proto, addr len(2), addr}.
std::string first_cdp_ipv4(const uint8_t* value, size_t length) {
    if (length < 4) {
        return {};
    }
    const uint32_t count = (static_cast<uint32_t>(value[0]) << 24) | (value[1] << 16) | (value[2] << 8) | value[3];
    size_t pos = 4;
    for (uint32_t i = 0; i < count && pos + 2 <= length; ++i) {
        const uint8_t proto_type = value[pos];
        const uint8_t proto_length = value[pos + 1];
        pos += 2;
        if (pos + proto_length + 2 > length) {
            return {};
        }
        const bool is_ip = proto_type == 1 && proto_length == 1 && value[pos] == 0xcc; // NLPID, IP
        pos += proto_length;
        const uint16_t address_length = read_u16(value + pos);
        pos += 2;
        if (pos + address_length > length) {
            return {};
        }
        if (is_ip && address_length == 4) {
            return format_ipv4(value + pos);
        }
        pos += address_length;
    }
    return {};
}

} // namespace

std::optional<Neighbor> parse_lldp_frame(const uint8_t* frame, size_t length) {
    size_t pos = 0;
    uint16_t ether_type = 0;
    if (!frame || !skip_ethernet_header(frame, length, pos, ether_type) || ether_type != kEtherTypeLldp) {
        return std::nullopt;
    }

    Neighbor neighbor;
    neighbor.protocol = Neighbor::Protocol::Lldp;
    bool have_chassis = false;
    bool have_port = false;
    // IEEE 802.1 VLAN TLVs can come in any order, so collect and resolve at the end.
    int32_t port_vlan = -1;          // subtype 1: Port VLAN ID (untagged/native)
    int32_t protocol_vlan = -1;      // subtype 2: first enabled Port and Protocol VLAN ID
    std::vector<std::pair<int32_t, std::string>> vlan_names; // subtype 3

    while (pos + 2 <= length) {
        const uint16_t header = read_u16(frame + pos);
        const uint8_t type = static_cast<uint8_t>(header >> 9);
        const size_t tlv_length = header & 0x1ff;
        pos += 2;
        if (type == 0) { // End of LLDPDU
            break;
        }
        if (pos + tlv_length > length) {
            return std::nullopt; // truncated
        }
        const uint8_t* value = frame + pos;
        pos += tlv_length;

        switch (type) {
            case 1: // Chassis ID
                if (tlv_length >= 2) {
                    neighbor.chassis_id = decode_lldp_id(value[0], value + 1, tlv_length - 1, false);
                    have_chassis = true;
                }
                break;
            case 2: // Port ID
                if (tlv_length >= 2) {
                    neighbor.port_id = decode_lldp_id(value[0], value + 1, tlv_length - 1, true);
                    have_port = true;
                }
                break;
            case 3: // Time To Live
                if (tlv_length >= 2) {
                    neighbor.ttl_seconds = read_u16(value);
                }
                break;
            case 4: // Port Description
                neighbor.port_description = clean_string(value, tlv_length);
                break;
            case 5: // System Name
                neighbor.system_name = clean_string(value, tlv_length);
                break;
            case 6: // System Description
                neighbor.platform = first_line(value, tlv_length);
                break;
            case 8: // Management Address: len(1, incl. subtype) subtype(1) address
                if (tlv_length >= 6 && value[0] == 5 && value[1] == 1 && neighbor.management_ip.empty()) {
                    neighbor.management_ip = format_ipv4(value + 2);
                }
                break;
            case 127: { // Organizationally specific
                if (tlv_length < 4) {
                    break;
                }
                const bool ieee_802_1 = value[0] == 0x00 && value[1] == 0x80 && value[2] == 0xc2;
                const bool ieee_802_3 = value[0] == 0x00 && value[1] == 0x12 && value[2] == 0x0f;
                const bool lldp_med = value[0] == 0x00 && value[1] == 0x12 && value[2] == 0xbb;
                if (ieee_802_3 && value[3] == 2 && tlv_length >= 7) {
                    // Power via MDI: support(1) pair(1) class(1, 1 = class 0 ... 5 = class 4),
                    // then with 802.3at: type(1) PD requested(2) PSE allocated(2), in 0.1 W.
                    if (value[6] >= 1 && value[6] <= 5) {
                        neighbor.poe_class = value[6] - 1;
                    }
                    if (tlv_length >= 12) {
                        const auto allocated = read_u16(value + 10);
                        if (allocated > 0) {
                            neighbor.poe_milliwatts = allocated * 100;
                        }
                    }
                    break;
                }
                if (lldp_med && value[3] == 4 && tlv_length >= 7 && neighbor.poe_milliwatts < 0) {
                    // Extended Power-via-MDI: type/source/priority(1) value(2, 0.1 W).
                    const auto power = read_u16(value + 5);
                    if (power > 0) {
                        neighbor.poe_milliwatts = power * 100;
                    }
                    break;
                }
                if (!ieee_802_1) {
                    break;
                }
                const uint8_t subtype = value[3];
                if (subtype == 1 && tlv_length >= 6) { // Port VLAN ID
                    const auto vlan = read_u16(value + 4);
                    if (vlan > 0 && vlan < 4095) {
                        port_vlan = vlan;
                    }
                }
                else if (subtype == 2 && tlv_length >= 7 && protocol_vlan < 0) { // Port and Protocol VLAN ID
                    const bool enabled = (value[4] & 0x04) != 0;
                    const auto vlan = read_u16(value + 5);
                    if (enabled && vlan > 0 && vlan < 4095) {
                        protocol_vlan = vlan;
                    }
                }
                else if (subtype == 3 && tlv_length >= 7) { // VLAN Name: id(2) length(1) name
                    const auto vlan = read_u16(value + 4);
                    const size_t name_length = std::min<size_t>(value[6], tlv_length - 7);
                    auto name = clean_string(value + 7, name_length);
                    if (vlan > 0 && vlan < 4095 && !name.empty()) {
                        vlan_names.emplace_back(vlan, std::move(name));
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    if (!have_chassis && !have_port) {
        return std::nullopt;
    }

    // Prefer the untagged Port VLAN ID; fall back to the protocol VLAN, then to
    // the only VLAN a name was advertised for (some switches send names only).
    neighbor.vlan = port_vlan >= 0 ? port_vlan : protocol_vlan;
    if (neighbor.vlan < 0 && vlan_names.size() == 1) {
        neighbor.vlan = vlan_names.front().first;
    }
    for (const auto& [vlan, name] : vlan_names) {
        if (vlan == neighbor.vlan) {
            neighbor.vlan_name = name;
            break;
        }
    }
    return neighbor;
}

std::optional<Neighbor> parse_cdp_frame(const uint8_t* frame, size_t length) {
    size_t pos = 0;
    uint16_t length_field = 0;
    if (!frame || length < 14 || std::memcmp(frame, kCdpMulticast.data(), kCdpMulticast.size()) != 0 ||
        !skip_ethernet_header(frame, length, pos, length_field) || length_field > 1500) {
        return std::nullopt;
    }
    // 802.2 LLC + SNAP: AA AA 03, OUI 00-00-0C, PID 0x2000; then CDP version, TTL, checksum.
    static constexpr std::array<uint8_t, 8> kSnapHeader = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x0c, 0x20, 0x00};
    if (pos + kSnapHeader.size() + 4 > length ||
        std::memcmp(frame + pos, kSnapHeader.data(), kSnapHeader.size()) != 0) {
        return std::nullopt;
    }
    pos += kSnapHeader.size();
    const uint8_t version = frame[pos];
    if (version != 1 && version != 2) {
        return std::nullopt;
    }

    Neighbor neighbor;
    neighbor.protocol = Neighbor::Protocol::Cdp;
    neighbor.ttl_seconds = frame[pos + 1];
    pos += 4;

    // The 802.3 length field bounds the payload; ignore Ethernet padding past it.
    const size_t end = std::min(length, static_cast<size_t>(pos - kSnapHeader.size() - 4 + length_field));
    bool have_device = false;

    while (pos + 4 <= end) {
        const uint16_t type = read_u16(frame + pos);
        const uint16_t tlv_length = read_u16(frame + pos + 2);
        if (tlv_length < 4 || pos + tlv_length > end) {
            break;
        }
        const uint8_t* value = frame + pos + 4;
        const size_t value_length = tlv_length - 4;
        pos += tlv_length;

        switch (type) {
            case 0x0001: // Device ID
                neighbor.system_name = clean_string(value, value_length);
                have_device = true;
                break;
            case 0x0002: // Addresses
                if (neighbor.management_ip.empty()) {
                    neighbor.management_ip = first_cdp_ipv4(value, value_length);
                }
                break;
            case 0x0003: // Port ID
                neighbor.port_id = clean_string(value, value_length);
                break;
            case 0x0006: // Platform
                neighbor.platform = clean_string(value, value_length);
                break;
            case 0x000a: // Native VLAN
                if (value_length >= 2) {
                    neighbor.vlan = read_u16(value);
                }
                break;
            case 0x0010: // Power consumption, in mW
                if (value_length >= 2 && read_u16(value) > 0) {
                    neighbor.poe_milliwatts = read_u16(value);
                }
                break;
            case 0x0016: // Management addresses (preferred over 0x0002)
            {
                auto address = first_cdp_ipv4(value, value_length);
                if (!address.empty()) {
                    neighbor.management_ip = std::move(address);
                }
                break;
            }
            default:
                break;
        }
    }

    if (!have_device) {
        return std::nullopt;
    }
    return neighbor;
}

NeighborListener::~NeighborListener() {
    stop();
}

void NeighborListener::start(const std::string& interface_name) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Idle && interface_name_ == interface_name) {
            return;
        }
    }
    stop();

    std::lock_guard<std::mutex> lock(mutex_);
    interface_name_ = interface_name;
    error_.clear();
    neighbor_.reset();
    last_update_ = Clock::now();
#if defined(__linux__)
    state_ = State::Listening;
    stop_requested_ = false;
    thread_ = std::thread(&NeighborListener::run, this, interface_name);
#else
    state_ = State::Unsupported;
#endif
    ++revision_;
}

void NeighborListener::stop() {
    stop_requested_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::Idle) {
        state_ = State::Idle;
        interface_name_.clear();
        neighbor_.reset();
        error_.clear();
        ++revision_;
    }
}

NeighborListener::Status NeighborListener::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Status status;
    status.state = state_;
    status.interface_name = interface_name_;
    status.error = error_;
    status.neighbor = neighbor_;
    if (state_ == State::Listening || state_ == State::Found) {
        status.seconds_since_update = static_cast<int32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - last_update_).count());
    }
    return status;
}

uint32_t NeighborListener::revision() const {
    return revision_.load();
}

void NeighborListener::set_error(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = State::Error;
    error_ = message;
    ++revision_;
}

void NeighborListener::set_neighbor(Neighbor neighbor) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = Clock::now();
    const bool have_fresh_lldp =
        neighbor_ && neighbor_->protocol == Neighbor::Protocol::Lldp &&
        (neighbor_->ttl_seconds <= 0 || now - last_update_ < std::chrono::seconds(neighbor_->ttl_seconds));

    if (neighbor.protocol == Neighbor::Protocol::Cdp && have_fresh_lldp) {
        // LLDP is the primary source; CDP only fills in what LLDP left out.
        if (neighbor_->vlan < 0) {
            neighbor_->vlan = neighbor.vlan;
            neighbor_->vlan_name.clear();
        }
        if (neighbor_->management_ip.empty()) {
            neighbor_->management_ip = neighbor.management_ip;
        }
        if (neighbor_->platform.empty()) {
            neighbor_->platform = neighbor.platform;
        }
        if (neighbor_->poe_milliwatts < 0) {
            neighbor_->poe_milliwatts = neighbor.poe_milliwatts;
        }
    }
    else {
        neighbor_ = std::move(neighbor);
    }
    state_ = State::Found;
    last_update_ = now;
    ++revision_;
}

#if defined(__linux__)

namespace {

int open_packet_socket(int if_index, uint16_t protocol, const std::array<uint8_t, 6>& multicast, std::string& error) {
    const int fd = ::socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(protocol));
    if (fd < 0) {
        error = errno == EPERM || errno == EACCES ? "needs root (CAP_NET_RAW)" : std::strerror(errno);
        return -1;
    }

    sockaddr_ll address{};
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(protocol);
    address.sll_ifindex = if_index;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = std::string("bind: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }

    // Make the NIC accept the link-local multicast group; fall back to all-multicast.
    packet_mreq membership{};
    membership.mr_ifindex = if_index;
    membership.mr_type = PACKET_MR_MULTICAST;
    membership.mr_alen = static_cast<unsigned short>(multicast.size());
    std::memcpy(membership.mr_address, multicast.data(), multicast.size());
    if (::setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0) {
        packet_mreq all_multicast{};
        all_multicast.mr_ifindex = if_index;
        all_multicast.mr_type = PACKET_MR_ALLMULTI;
        ::setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &all_multicast, sizeof(all_multicast));
    }
    return fd;
}

} // namespace

void NeighborListener::run(std::string interface_name) {
    const auto if_index = static_cast<int>(::if_nametoindex(interface_name.c_str()));
    if (if_index == 0) {
        set_error("unknown interface " + interface_name);
        return;
    }

    std::string error;
    const int lldp_fd = open_packet_socket(if_index, kEtherTypeLldp, kLldpMulticast, error);
    if (lldp_fd < 0) {
        set_error(error);
        return;
    }
    // CDP rides on 802.2 LLC/SNAP frames; ETH_P_802_2 delivers all 802.3-length frames.
    const int cdp_fd = open_packet_socket(if_index, ETH_P_802_2, kCdpMulticast, error);

    std::array<pollfd, 2> fds{{{lldp_fd, POLLIN, 0}, {cdp_fd, POLLIN, 0}}};
    const nfds_t fd_count = cdp_fd >= 0 ? 2 : 1;
    std::vector<uint8_t> buffer(2048);

    while (!stop_requested_) {
        const int ready = ::poll(fds.data(), fd_count, 250);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(std::string("poll: ") + std::strerror(errno));
            break;
        }
        for (nfds_t i = 0; i < fd_count; ++i) {
            if (!(fds[i].revents & POLLIN)) {
                continue;
            }
            const auto received = ::recv(fds[i].fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
            if (received <= 0) {
                continue;
            }
            const auto size = static_cast<size_t>(received);
            auto neighbor = i == 0 ? parse_lldp_frame(buffer.data(), size) : parse_cdp_frame(buffer.data(), size);
            if (neighbor) {
                set_neighbor(std::move(*neighbor));
            }
        }
    }

    ::close(lldp_fd);
    if (cdp_fd >= 0) {
        ::close(cdp_fd);
    }
}

#else

void NeighborListener::run(std::string) {}

#endif

} // namespace platform::net
