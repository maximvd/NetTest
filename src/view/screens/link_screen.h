/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "network_viewmodel.h"

#include <array>

namespace screen {

// Link status for one kind of interface (the Ethernet page or the Wi-Fi page):
// the selected interface's state and addresses on the left, and the switch/port
// it is plugged into (Ethernet) or SSID/signal (Wi-Fi) on the right.
class LinkScreen : public BaseScreen {
public:
    LinkScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, viewmodel::LinkKind kind);

private:
    static constexpr size_t kRows = 5;

    struct Row {
        lv_obj_t* key{nullptr};
        lv_obj_t* value{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    void refresh();
    static void link_changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::LinkKind kind_;
    lv_obj_t* interface_label_{nullptr};
    lv_obj_t* state_label_{nullptr};
    lv_obj_t* index_label_{nullptr};
    std::array<Row, kRows> left_{};
    std::array<Row, kRows> right_{};
};

} // namespace screen
