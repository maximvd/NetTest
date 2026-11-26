/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bento_screen.h"
#include "network_viewmodel.h"

#include <array>

namespace screen {

// Link status for one kind of interface (the Ethernet page or the Wi-Fi page)
// as bento tiles. Top row: the switch port this jack leads to, with the
// switch name and a VLAN block, and the link speed (Ethernet); or the network
// and its signal (Wi-Fi). Bottom row: the address, gateway and DNS of the
// selected interface. Each tile opens into a panel with the full details.
class LinkScreen : public BentoScreen {
public:
    LinkScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, viewmodel::LinkKind kind);

private:
    static constexpr size_t kSignalBars = 4;

    void build_content(lv_obj_t* content) override;
    void refresh_tiles() override;
    bento::Detail detail(size_t tile) override;

    void build_vlan_block(const bento::Fonts& fonts);
    void build_signal_bars();
    void refresh_wired(const platform::net::InterfaceInfo& info, const view::ThemePalette& colors);
    void refresh_wireless(const platform::net::InterfaceInfo& info, const view::ThemePalette& colors);
    void refresh_addresses(const platform::net::InterfaceInfo& info, const view::ThemePalette& colors);
    void show_tiles(bool visible);

    bento::Detail switch_detail(const platform::net::InterfaceInfo& info);
    bento::Detail link_detail(const platform::net::InterfaceInfo& info);
    bento::Detail wifi_detail(const platform::net::InterfaceInfo& info);
    bento::Detail address_detail(const platform::net::InterfaceInfo& info);
    bento::Detail gateway_detail(const platform::net::InterfaceInfo& info);
    bento::Detail dns_detail();

    viewmodel::LinkKind kind_;
    lv_obj_t* empty_label_{nullptr};
    bento::Tile main_;    // switch port (Ethernet) or network name (Wi-Fi)
    bento::Tile side_;    // link speed (Ethernet) or signal (Wi-Fi)
    bento::Tile address_;
    bento::Tile gateway_;
    bento::Tile dns_;

    struct VlanBlock {
        lv_obj_t* box{nullptr};
        lv_obj_t* caption{nullptr};
        lv_obj_t* number{nullptr};
        lv_obj_t* name{nullptr};
    } vlan_;
    std::array<lv_obj_t*, kSignalBars> signal_bars_{};
};

} // namespace screen
