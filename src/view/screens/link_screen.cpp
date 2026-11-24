/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "screen_utils.h"
#include "theme.h"

#include <string>
#include <utility>

namespace screen {
namespace {

using platform::net::InterfaceInfo;
using platform::net::Neighbor;
using platform::net::NeighborListener;

constexpr int32_t kPadX = 8;
constexpr int32_t kHeaderY = 3;
constexpr int32_t kRowsY = 25;
constexpr int32_t kRowPitch = 16;
constexpr int32_t kColumnWidth = 152;
constexpr int32_t kKeyWidth = 36;
constexpr int32_t kInterfaceNameWidth = 90;
constexpr int32_t kStateX = kPadX + kInterfaceNameWidth + 4;
constexpr int32_t kStateWidth = 150;
constexpr int32_t kIndexWidth = 40;
constexpr int32_t kRightColumnX = kPadX + kColumnWidth + 4;

constexpr const char* kNone = "-";

using KeyValue = std::pair<std::string, std::string>;

std::string or_none(const std::string& value) {
    return value.empty() ? kNone : value;
}

std::string link_state_text(const InterfaceInfo& info) {
    if (!info.up) {
        return "DOWN";
    }
    if (!info.carrier) {
        return info.wireless ? "NOT CONNECTED" : "NO CABLE";
    }
    if (info.wireless) {
        return "WI-FI";
    }
    std::string text = "UP";
    if (info.speed_mbps > 0) {
        text += info.speed_mbps >= 1000 && info.speed_mbps % 1000 == 0
                    ? "  " + std::to_string(info.speed_mbps / 1000) + "G"
                    : "  " + std::to_string(info.speed_mbps) + "M";
    }
    if (info.duplex == "full") {
        text += " FD";
    }
    else if (info.duplex == "half") {
        text += " HD"; // half duplex usually means a negotiation problem: worth seeing
    }
    return text;
}

std::string ipv4_text(const InterfaceInfo& info) {
    if (info.ipv4.empty()) {
        return info.carrier ? "no address" : kNone;
    }
    return info.prefix_length >= 0 ? info.ipv4 + "/" + std::to_string(info.prefix_length) : info.ipv4;
}

std::string dns_text(const std::vector<std::string>& servers) {
    if (servers.empty()) {
        return kNone;
    }
    std::string text = servers.front();
    if (servers.size() > 1) {
        text += " +" + std::to_string(servers.size() - 1);
    }
    return text;
}

std::string age_text(const char* prefix, int32_t seconds) {
    if (seconds < 0) {
        return prefix;
    }
    if (seconds < 120) {
        return std::string(prefix) + " " + std::to_string(seconds) + "s ago";
    }
    return std::string(prefix) + " " + std::to_string(seconds / 60) + "m ago";
}

std::string vlan_text(const Neighbor& neighbor) {
    if (neighbor.vlan < 0) {
        return neighbor.vlan_name.empty() ? kNone : neighbor.vlan_name;
    }
    const auto id = std::to_string(neighbor.vlan);
    return neighbor.vlan_name.empty() ? id : id + " " + neighbor.vlan_name;
}

std::array<KeyValue, 5> wireless_rows(const InterfaceInfo& info) {
    std::string signal = kNone;
    if (info.has_signal) {
        signal = std::to_string(info.signal_dbm) + " dBm  " +
                 std::to_string(platform::net::signal_quality_percent(info.signal_dbm)) + "%";
    }
    return {{
        {"SSID", or_none(info.ssid)},
        {"SIG", signal},
        {"MTU", info.mtu > 0 ? std::to_string(info.mtu) : kNone},
        {"", ""},
        {"", ""},
    }};
}

std::array<KeyValue, 5> switch_rows(const InterfaceInfo& info, const NeighborListener::Status& status) {
    using State = NeighborListener::State;
    if (!info.carrier) {
        return {{{"SW", kNone}, {"PORT", kNone}, {"VLAN", kNone}, {"", ""}, {"LLDP", "no link"}}};
    }
    switch (status.state) {
        case State::Found: {
            const auto& neighbor = *status.neighbor;
            const char* protocol = neighbor.protocol == Neighbor::Protocol::Lldp ? "LLDP" : "CDP";
            // Port ID is often a MAC or ifIndex; the description is then the readable name.
            std::string port = or_none(neighbor.port_id);
            if (!neighbor.port_description.empty() && neighbor.port_description != neighbor.port_id) {
                port += "  " + neighbor.port_description;
            }
            return {{
                {"SW", or_none(neighbor.system_name.empty() ? neighbor.chassis_id : neighbor.system_name)},
                {"PORT", port},
                {"VLAN", vlan_text(neighbor)},
                {"MGMT", or_none(neighbor.management_ip)},
                {"SEEN", age_text(protocol, status.seconds_since_update)},
            }};
        }
        case State::Listening:
            return {{
                {"SW", "listening..."},
                {"PORT", kNone},
                {"VLAN", kNone},
                {"", ""},
                {"LLDP", status.seconds_since_update >= 0
                             ? "waiting " + std::to_string(status.seconds_since_update) + "s"
                             : "waiting"},
            }};
        case State::Unsupported:
            return {{{"SW", "n/a"}, {"PORT", kNone}, {"VLAN", kNone}, {"", ""}, {"LLDP", "device only"}}};
        case State::Error:
            return {{{"SW", "error"}, {"PORT", kNone}, {"VLAN", kNone}, {"", ""}, {"LLDP", status.error}}};
        case State::Idle:
        default:
            return {{{"SW", kNone}, {"PORT", kNone}, {"VLAN", kNone}, {"", ""}, {"LLDP", "idle"}}};
    }
}

} // namespace

LinkScreen::LinkScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       viewmodel::LinkKind kind)
    : BaseScreen(view_model, assets), kind_(kind) {
    init();
}

void LinkScreen::build_content(lv_obj_t* content) {
    const lv_font_t* header_font = assets().load_standard_font(14);
    const lv_font_t* state_font = assets().load_standard_font(12);
    const lv_font_t* key_font = assets().load_standard_font(11);
    const lv_font_t* value_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    header_font = header_font ? header_font : &lv_font_montserrat_14;
    state_font = state_font ? state_font : &lv_font_montserrat_12;
    key_font = key_font ? key_font : &lv_font_montserrat_12;
    value_font = value_font ? value_font : &lv_font_montserrat_12;

    auto* dark_mode = view_model().dark_mode_subject();

    interface_label_ = util::create_label(content, header_font, kPadX, kHeaderY, kInterfaceNameWidth);
    reactive::bind_theme(interface_label_, dark_mode, reactive::ThemeRole::Text);
    state_label_ = util::create_label(content, state_font, kStateX, kHeaderY + 2, kStateWidth);
    index_label_ =
        util::create_label(content, state_font, view::kScreenWidth - kPadX - kIndexWidth, kHeaderY + 2, kIndexWidth);
    lv_obj_set_style_text_align(index_label_, LV_TEXT_ALIGN_RIGHT, 0);

    const auto build_column = [&](std::array<Row, kRows>& rows, int32_t x) {
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto y = kRowsY + static_cast<int32_t>(i) * kRowPitch;
            rows[i].key = util::create_label(content, key_font, x, y + 1, kKeyWidth, LV_LABEL_LONG_MODE_CLIP);
            rows[i].value = util::create_label(content,
                                               value_font,
                                               x + kKeyWidth,
                                               y,
                                               kColumnWidth - kKeyWidth,
                                               LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
            reactive::bind_theme(rows[i].value, dark_mode, reactive::ThemeRole::Text);
        }
    };
    build_column(left_, kPadX);
    build_column(right_, kRightColumnX);

    // A thin divider between the address and switch columns.
    auto* divider = lv_obj_create(content);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, 1, kRows * kRowPitch - 4);
    lv_obj_set_pos(divider, kRightColumnX - 4, kRowsY + 2);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(divider, view::palette(view_model().is_dark_mode()).border, 0);
    reactive::observe_obj(divider, dark_mode, [](lv_observer_t* observer, lv_subject_t* subject) {
        lv_obj_set_style_bg_color(lv_observer_get_target_obj(observer),
                                  view::palette(lv_subject_get_int(subject) != 0).border,
                                  0);
    });

    reactive::observe_obj(content, view_model().network().link_revision_subject(), link_changed_cb, this);
    reactive::observe_obj(content, dark_mode, link_changed_cb, this);
}

void LinkScreen::refresh() {
    auto& network = view_model().network();
    const auto& snapshot = network.snapshot();
    const auto* info = network.selected_interface(kind_);
    const auto colors = view::palette(view_model().is_dark_mode());

    if (!info) {
        lv_obj_set_width(interface_label_, view::kScreenWidth - 2 * kPadX);
        util::set_text(interface_label_,
                       kind_ == viewmodel::LinkKind::Wired ? "No Ethernet port" : "No Wi-Fi adapter");
        util::set_text(state_label_, "");
        util::set_text(index_label_, "0/0");
        lv_obj_set_style_text_color(index_label_, colors.text_disabled, 0);
        for (auto* column : {&left_, &right_}) {
            for (auto& row : *column) {
                util::set_visible(row.key, false);
                util::set_visible(row.value, false);
            }
        }
        return;
    }

    lv_obj_set_width(interface_label_, kInterfaceNameWidth);
    util::set_text(interface_label_, info->name);
    util::set_text(state_label_, link_state_text(*info));
    lv_obj_set_style_text_color(state_label_,
                                !info->up        ? colors.text_disabled
                                : info->carrier  ? colors.success
                                                 : colors.error,
                                0);

    // Position among this page's interfaces; up/down arrows move through them.
    const auto count = network.interfaces(kind_).size();
    util::set_text(index_label_, std::to_string(network.selected_position(kind_) + 1) + "/" + std::to_string(count));
    lv_obj_set_style_text_color(index_label_, colors.text_disabled, 0);

    const bool is_gateway_interface = info->name == snapshot.gateway_interface;
    const std::array<KeyValue, kRows> left_rows = {{
        {"IP", ipv4_text(*info)},
        {"GW", is_gateway_interface ? or_none(snapshot.gateway) : kNone},
        {"DNS", dns_text(snapshot.dns_servers)},
        {"MAC", or_none(info->mac)},
        {"IPv6", or_none(info->ipv6)},
    }};
    const auto right_rows = kind_ == viewmodel::LinkKind::Wireless ? wireless_rows(*info)
                                                                    : switch_rows(*info, network.neighbor_status());

    const auto apply = [&](std::array<Row, kRows>& rows, const std::array<KeyValue, kRows>& values) {
        for (size_t i = 0; i < rows.size(); ++i) {
            const bool visible = !values[i].first.empty();
            util::set_visible(rows[i].key, visible);
            util::set_visible(rows[i].value, visible);
            if (visible) {
                util::set_text(rows[i].key, values[i].first);
                util::set_text(rows[i].value, values[i].second);
                lv_obj_set_style_text_color(rows[i].key, colors.primary, 0);
            }
        }
    };
    apply(left_, left_rows);
    apply(right_, right_rows);
}

void LinkScreen::link_changed_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<LinkScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
