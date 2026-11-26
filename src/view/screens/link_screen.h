/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "bento.h"
#include "network_viewmodel.h"

#include <array>

namespace screen {

// Link status for one kind of interface (the Ethernet page or the Wi-Fi page)
// as bento tiles. Top row: the switch port this jack leads to and the link
// speed (Ethernet), or the network and its signal (Wi-Fi). Bottom row: the
// address, gateway and DNS of the selected interface.
class LinkScreen : public BaseScreen {
public:
    LinkScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, viewmodel::LinkKind kind);

private:
    static constexpr size_t kSignalBars = 4;

    void build_content(lv_obj_t* content) override;
    void refresh();
    void refresh_wired(const platform::net::InterfaceInfo& info, const view::ThemePalette& colors);
    void refresh_wireless(const platform::net::InterfaceInfo& info, const view::ThemePalette& colors);
    void refresh_addresses(const platform::net::InterfaceInfo& info, const view::ThemePalette& colors);
    void show_tiles(bool visible);
    static void link_changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::LinkKind kind_;
    lv_obj_t* empty_label_{nullptr};
    bento::Tile main_;    // switch port (Ethernet) or network name (Wi-Fi)
    bento::Tile side_;    // link speed (Ethernet) or signal (Wi-Fi)
    bento::Tile address_;
    bento::Tile gateway_;
    bento::Tile dns_;
    lv_obj_t* vlan_badge_{nullptr};
    std::array<lv_obj_t*, kSignalBars> signal_bars_{};
};

} // namespace screen
