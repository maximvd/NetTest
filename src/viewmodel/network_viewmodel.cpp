/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "network_viewmodel.h"

#include <algorithm>

namespace viewmodel {
namespace {

constexpr uint32_t kPollPeriodMs = 250;
constexpr uint32_t kSnapshotEveryTicks = 4; // 1 s

bool is_kind(const platform::net::InterfaceInfo& info, LinkKind kind) {
    return info.wireless == (kind == LinkKind::Wireless);
}

} // namespace

NetworkViewModel::NetworkViewModel() {
    refresh_snapshot();
    timer_ = lv_timer_create(timer_cb, kPollPeriodMs, this);
}

NetworkViewModel::~NetworkViewModel() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    // Member destructors stop and join the worker threads.
}

lv_subject_t* NetworkViewModel::link_revision_subject() {
    return link_revision_subject_.native();
}

lv_subject_t* NetworkViewModel::tests_revision_subject() {
    return tests_revision_subject_.native();
}

const platform::net::NetSnapshot& NetworkViewModel::snapshot() const {
    return snapshot_;
}

NetworkViewModel::Selection& NetworkViewModel::selection(LinkKind kind) {
    return selections_[static_cast<size_t>(kind)];
}

const NetworkViewModel::Selection& NetworkViewModel::selection(LinkKind kind) const {
    return selections_[static_cast<size_t>(kind)];
}

std::vector<const platform::net::InterfaceInfo*> NetworkViewModel::interfaces(LinkKind kind) const {
    std::vector<const platform::net::InterfaceInfo*> result;
    for (const auto& info : snapshot_.interfaces) {
        if (is_kind(info, kind)) {
            result.push_back(&info);
        }
    }
    return result;
}

size_t NetworkViewModel::selected_position(LinkKind kind) const {
    const auto list = interfaces(kind);
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i]->name == selection(kind).name) {
            return i;
        }
    }
    return 0;
}

const platform::net::InterfaceInfo* NetworkViewModel::selected_interface(LinkKind kind) const {
    const auto list = interfaces(kind);
    for (const auto* info : list) {
        if (info->name == selection(kind).name) {
            return info;
        }
    }
    return list.empty() ? nullptr : list.front();
}

void NetworkViewModel::select_adjacent_interface(LinkKind kind, int step) {
    const auto list = interfaces(kind);
    if (list.size() < 2) {
        return;
    }
    const auto count = static_cast<int>(list.size());
    const auto next = ((static_cast<int>(selected_position(kind)) + step) % count + count) % count;
    auto& current = selection(kind);
    current.name = list[static_cast<size_t>(next)]->name;
    current.user_selected = true;
    update_neighbor_listener();
    link_revision_subject_.set(link_revision_subject_.value() + 1);
}

LinkKind NetworkViewModel::default_route_kind() const {
    for (const auto& info : snapshot_.interfaces) {
        if (info.name == snapshot_.gateway_interface) {
            return info.wireless ? LinkKind::Wireless : LinkKind::Wired;
        }
    }
    return LinkKind::Wired;
}

platform::net::NeighborListener::Status NetworkViewModel::neighbor_status() const {
    auto status = neighbor_listener_.status();
    if (status.interface_name != selection(LinkKind::Wired).name) {
        return {};
    }
    return status;
}

platform::net::ConnectivityTester::Report NetworkViewModel::test_report() const {
    return tester_.report();
}

void NetworkViewModel::run_tests() {
    refresh_snapshot();
    // DHCP is counted on the uplink; with no default route, on the cabled Ethernet port.
    std::string dhcp_interface = snapshot_.gateway_interface;
    if (dhcp_interface.empty()) {
        const auto* wired = selected_interface(LinkKind::Wired);
        if (wired && wired->carrier) {
            dhcp_interface = wired->name;
        }
    }
    tester_.start(snapshot_.gateway, snapshot_.dns_servers, dhcp_interface);
}

void NetworkViewModel::timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<NetworkViewModel*>(lv_timer_get_user_data(timer));
    if (self) {
        self->poll();
    }
}

void NetworkViewModel::poll() {
    bool link_changed = false;
    if (++tick_ >= kSnapshotEveryTicks) {
        tick_ = 0;
        refresh_snapshot();
        link_changed = true; // also advances the "seen N s ago" counter
    }

    const auto neighbor_revision = neighbor_listener_.revision();
    if (neighbor_revision != seen_neighbor_revision_) {
        seen_neighbor_revision_ = neighbor_revision;
        link_changed = true;
    }
    if (link_changed) {
        link_revision_subject_.set(link_revision_subject_.value() + 1);
    }

    const auto tests_revision = tester_.revision();
    if (tests_revision != seen_tests_revision_) {
        seen_tests_revision_ = tests_revision;
        tests_revision_subject_.set(tests_revision_subject_.value() + 1);
    }
}

int32_t NetworkViewModel::link_drops(const platform::net::InterfaceInfo& info) const {
    const auto baseline = drop_baselines_.find(info.name);
    if (info.carrier_down_count < 0 || baseline == drop_baselines_.end()) {
        return -1;
    }
    return std::max(0, info.carrier_down_count - baseline->second);
}

void NetworkViewModel::refresh_snapshot() {
    snapshot_ = platform::net::read_snapshot();
    for (const auto& info : snapshot_.interfaces) {
        if (info.carrier_down_count >= 0) {
            drop_baselines_.emplace(info.name, info.carrier_down_count); // keeps the first value
        }
    }
    update_selection(LinkKind::Wired);
    update_selection(LinkKind::Wireless);
    update_neighbor_listener();
}

void NetworkViewModel::update_selection(LinkKind kind) {
    auto& current = selection(kind);
    const auto list = interfaces(kind);
    if (list.empty()) {
        current = {};
        return;
    }
    const bool still_present = std::any_of(list.begin(), list.end(), [&current](const auto* info) {
        return info->name == current.name;
    });
    if (current.user_selected && still_present) {
        return;
    }
    // Until the user picks one: the interface carrying the default route, else
    // the first one with link, else the first one.
    const platform::net::InterfaceInfo* preferred = nullptr;
    for (const auto* info : list) {
        if (info->name == snapshot_.gateway_interface) {
            preferred = info;
            break;
        }
    }
    if (!preferred) {
        const auto with_link = std::find_if(list.begin(), list.end(), [](const auto* info) { return info->carrier; });
        preferred = with_link != list.end() ? *with_link : list.front();
    }
    current.name = preferred->name;
    current.user_selected = false;
}

void NetworkViewModel::update_neighbor_listener() {
    // Switches only advertise on wired ports, so follow the selected Ethernet interface.
    const auto* info = selected_interface(LinkKind::Wired);
    if (!info) {
        neighbor_listener_.stop();
        return;
    }
    if (!info->carrier) {
        // Cable unplugged: forget the old switch so a re-plug starts fresh.
        if (neighbor_listener_.status().interface_name == info->name) {
            neighbor_listener_.stop();
        }
        return;
    }
    neighbor_listener_.start(info->name);
}

} // namespace viewmodel
