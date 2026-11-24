/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "neighbor_discovery.h"
#include "net_info.h"
#include "net_probe.h"
#include "subjects.h"

#include "lvgl.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace viewmodel {

enum class LinkKind { Wired = 0, Wireless = 1 };

// Owns the long-lived network services so they survive page switches, and
// turns their thread-side updates into LVGL subject notifications on the UI
// thread. Screens observe the two revision subjects and pull the data.
class NetworkViewModel {
public:
    NetworkViewModel();
    ~NetworkViewModel();

    NetworkViewModel(const NetworkViewModel&) = delete;
    NetworkViewModel& operator=(const NetworkViewModel&) = delete;

    // Bumped when link data (interfaces, routes, DNS, switch/port) may have changed.
    lv_subject_t* link_revision_subject();
    // Bumped when the connectivity test report changed.
    lv_subject_t* tests_revision_subject();

    const platform::net::NetSnapshot& snapshot() const;

    // Interfaces of one kind (Ethernet or Wi-Fi), each kind with its own
    // selection. selected_interface() is nullptr when there are none.
    std::vector<const platform::net::InterfaceInfo*> interfaces(LinkKind kind) const;
    const platform::net::InterfaceInfo* selected_interface(LinkKind kind) const;
    size_t selected_position(LinkKind kind) const; // 0-based, within interfaces(kind)
    // Moves the selection by `step` (+1 next, -1 previous), wrapping around.
    void select_adjacent_interface(LinkKind kind, int step);

    // Kind of the interface carrying the default route (Wired when unknown).
    LinkKind default_route_kind() const;

    // Neighbor (switch/port) status for the selected wired interface. State is
    // Idle when there is no wired interface or it has no link.
    platform::net::NeighborListener::Status neighbor_status() const;

    platform::net::ConnectivityTester::Report test_report() const;
    void run_tests();

private:
    struct Selection {
        std::string name;
        bool user_selected{false};
    };

    static void timer_cb(lv_timer_t* timer);
    void poll();
    void refresh_snapshot();
    void update_selection(LinkKind kind);
    void update_neighbor_listener();
    Selection& selection(LinkKind kind);
    const Selection& selection(LinkKind kind) const;

    platform::net::NetSnapshot snapshot_;
    std::array<Selection, 2> selections_{};

    platform::net::NeighborListener neighbor_listener_;
    platform::net::ConnectivityTester tester_;
    uint32_t seen_neighbor_revision_{0};
    uint32_t seen_tests_revision_{0};

    reactive::IntSubject link_revision_subject_{0};
    reactive::IntSubject tests_revision_subject_{0};
    lv_timer_t* timer_{nullptr};
    uint32_t tick_{0};
};

} // namespace viewmodel
