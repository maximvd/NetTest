/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "screen_utils.h"

#include <array>
#include <cstdio>
#include <string>

namespace screen {
namespace {

using bento::Detail;
using bento::DetailRow;
using bento::Tone;
using platform::net::ConnectivityTester;
using platform::net::InterfaceInfo;
using platform::net::Neighbor;
using platform::net::NeighborListener;
using util::age_text;
using util::join;

constexpr const char* kNone = "-";
constexpr int32_t kVlanBlockWidth = 50;
constexpr int32_t kVlanBlockInset = 4;
constexpr int32_t kNameY = 42; // the switch name, under the port in the taller top tile
constexpr int32_t kBarWidth = 5;
constexpr int32_t kBarGap = 2;
constexpr int32_t kBarMaxHeight = 20;

std::string or_none(const std::string& value) {
    return value.empty() ? kNone : value;
}

std::string speed_text(int32_t mbps) {
    if (mbps <= 0) {
        return "UP";
    }
    if (mbps >= 1000 && mbps % 1000 == 0) {
        return std::to_string(mbps / 1000) + "G";
    }
    if (mbps >= 1000 && mbps % 100 == 0) {
        return std::to_string(mbps / 1000) + "." + std::to_string(mbps % 1000 / 100) + "G";
    }
    return std::to_string(mbps) + "M";
}

bool is_link_local_ipv4(const std::string& address) {
    return address.rfind("169.254.", 0) == 0;
}

std::string switch_name(const Neighbor& neighbor) {
    return neighbor.system_name.empty() ? neighbor.chassis_id : neighbor.system_name;
}

std::string watts_text(int32_t milliwatts) {
    const auto tenths = (milliwatts + 50) / 100;
    return std::to_string(tenths / 10) + (tenths % 10 ? "." + std::to_string(tenths % 10) : "") + " W";
}

// The PoE power the switch gives this port: "PoE 30 W", "PoE class 3" or "".
std::string poe_text(const Neighbor& neighbor) {
    if (neighbor.poe_milliwatts > 0) {
        return "PoE " + watts_text(neighbor.poe_milliwatts);
    }
    return neighbor.poe_class >= 0 ? "PoE class " + std::to_string(neighbor.poe_class) : "";
}

std::string vlan_text(const Neighbor& neighbor) {
    if (neighbor.vlan < 0) {
        return or_none(neighbor.vlan_name);
    }
    return join({std::to_string(neighbor.vlan), neighbor.vlan_name});
}

// The first line of the platform / system description, e.g. "C9300-48P".
std::string model_text(const Neighbor& neighbor) {
    const auto& platform = neighbor.platform;
    return platform.substr(0, platform.find('\n'));
}

std::string duplex_text(const InterfaceInfo& info) {
    return info.duplex.empty() ? "" : info.duplex + " duplex";
}

// The last check's figure for a step, if the checks have run and it passed.
std::string last_check(const ConnectivityTester::Report& report, ConnectivityTester::Step step) {
    if (report.runs == 0 || report.running) {
        return "";
    }
    const auto& result = report.steps[step];
    const bool usable = result.state == ConnectivityTester::StepState::Ok ||
                        result.state == ConnectivityTester::StepState::Warn;
    return usable ? result.headline : "";
}

// Netmask and broadcast for an IPv4 address and prefix length; empty when unparsable.
std::pair<std::string, std::string> mask_and_broadcast(const std::string& ipv4, int32_t prefix) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (prefix < 0 || prefix > 32 || std::sscanf(ipv4.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return {"", ""};
    }
    const uint32_t address = (a << 24) | (b << 16) | (c << 8) | d;
    const uint32_t mask = prefix == 0 ? 0 : 0xffffffffu << (32 - prefix);
    const auto format = [](uint32_t value) {
        return std::to_string(value >> 24) + "." + std::to_string((value >> 16) & 0xff) + "." +
               std::to_string((value >> 8) & 0xff) + "." + std::to_string(value & 0xff);
    };
    return {format(mask), format(address | ~mask)};
}

} // namespace

LinkScreen::LinkScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       viewmodel::LinkKind kind)
    : BentoScreen(view_model, assets), kind_(kind) {
    init();
}

void LinkScreen::build_content(lv_obj_t* content) {
    const auto fonts = bento::Fonts::load(assets());

    main_ = bento::create_tile(content, fonts, {0, 0, 2}, bento::ValueSize::Large);
    side_ = bento::create_tile(content, fonts, {2, 0}, bento::ValueSize::Large);
    address_ = bento::create_tile(content, fonts, {0, 1}, bento::ValueSize::Medium);
    gateway_ = bento::create_tile(content, fonts, {1, 1}, bento::ValueSize::Medium);
    dns_ = bento::create_tile(content, fonts, {2, 1}, bento::ValueSize::Medium);

    if (kind_ == viewmodel::LinkKind::Wired) {
        // The switch name gets its own, larger line under the port.
        lv_obj_set_style_text_font(main_.detail, fonts.name, 0);
        lv_obj_set_y(main_.detail, kNameY);
        build_vlan_block(fonts);
    }
    else {
        build_signal_bars();
    }

    empty_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(empty_label_, fonts.value_medium, 0);
    lv_label_set_text(empty_label_, kind_ == viewmodel::LinkKind::Wired ? "No Ethernet port" : "No Wi-Fi adapter");
    lv_obj_center(empty_label_);

    attach_tiles(content, fonts, {&main_, &side_, &address_, &gateway_, &dns_});
    auto& network = view_model().network();
    reactive::observe_obj(content, network.link_revision_subject(), changed_cb, this);
    reactive::observe_obj(content, network.tests_revision_subject(), changed_cb, this);
}

void LinkScreen::build_vlan_block(const bento::Fonts& fonts) {
    vlan_.box = lv_obj_create(main_.box);
    lv_obj_remove_style_all(vlan_.box);
    lv_obj_clear_flag(vlan_.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(vlan_.box, kVlanBlockWidth, main_.height - 2 * kVlanBlockInset);
    lv_obj_align(vlan_.box, LV_ALIGN_RIGHT_MID, -kVlanBlockInset, 0);
    lv_obj_set_style_radius(vlan_.box, 4, 0);
    lv_obj_set_style_bg_opa(vlan_.box, LV_OPA_20, 0);
    lv_obj_set_flex_flow(vlan_.box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(vlan_.box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    vlan_.caption = lv_label_create(vlan_.box);
    lv_label_set_text(vlan_.caption, "VLAN");
    lv_obj_set_style_text_font(vlan_.caption, fonts.label, 0);
    vlan_.number = lv_label_create(vlan_.box);
    lv_label_set_text(vlan_.number, "");
    lv_obj_set_style_text_font(vlan_.number, fonts.value_large, 0);
    vlan_.name = lv_label_create(vlan_.box);
    lv_label_set_text(vlan_.name, "");
    lv_obj_set_style_text_font(vlan_.name, fonts.label, 0);
    lv_obj_set_width(vlan_.name, kVlanBlockWidth - 4);
    lv_obj_set_style_text_align(vlan_.name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(vlan_.name, LV_LABEL_LONG_MODE_SCROLL);
}

void LinkScreen::build_signal_bars() {
    // Signal bars in the network tile's top-right corner, short to tall.
    main_.reserve_right(static_cast<int32_t>(kSignalBars) * (kBarWidth + kBarGap) + 4);
    for (size_t i = 0; i < signal_bars_.size(); ++i) {
        auto* bar = lv_obj_create(main_.box);
        lv_obj_remove_style_all(bar);
        const auto height = kBarMaxHeight * static_cast<int32_t>(i + 1) / static_cast<int32_t>(kSignalBars);
        lv_obj_set_size(bar, kBarWidth, height);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        const auto x_from_right = static_cast<int32_t>(kSignalBars - 1 - i) * (kBarWidth + kBarGap) + 6;
        lv_obj_align(bar, LV_ALIGN_TOP_RIGHT, -x_from_right, 6 + kBarMaxHeight - height);
        signal_bars_[i] = bar;
    }
}

void LinkScreen::show_tiles(bool visible) {
    for (const auto* tile : {&main_, &side_, &address_, &gateway_, &dns_}) {
        util::set_visible(tile->box, visible);
    }
    util::set_visible(empty_label_, !visible);
}

void LinkScreen::refresh_tiles() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto* info = view_model().network().selected_interface(kind_);

    lv_obj_set_style_text_color(empty_label_, colors.text_muted, 0);
    show_tiles(info != nullptr);
    if (!info) {
        return;
    }

    if (kind_ == viewmodel::LinkKind::Wired) {
        refresh_wired(*info, colors);
    }
    else {
        refresh_wireless(*info, colors);
    }
    refresh_addresses(*info, colors);
}

void LinkScreen::refresh_wired(const InterfaceInfo& info, const view::ThemePalette& colors) {
    using State = NeighborListener::State;
    const auto status = view_model().network().neighbor_status();
    const Neighbor* neighbor = status.state == State::Found && status.neighbor ? &*status.neighbor : nullptr;

    // Link speed, or why there is no link. Under it the most telling extra:
    // half duplex, then link drops, then PoE.
    if (!info.up) {
        side_.set("LINK", "Down", Tone::Muted, "interface off", colors);
    }
    else if (!info.carrier) {
        side_.set("LINK", "No cable", Tone::Error, "plug in a cable", colors);
    }
    else {
        const auto drops = view_model().network().link_drops(info);
        std::string extra = duplex_text(info);
        if (info.duplex == "half") {
            extra = "HALF duplex";
        }
        else if (drops > 0) {
            extra = std::to_string(drops) + (drops == 1 ? " drop" : " drops");
        }
        else if (neighbor && !poe_text(*neighbor).empty()) {
            extra = poe_text(*neighbor);
        }
        side_.set("LINK",
                  speed_text(info.speed_mbps),
                  info.duplex == "half" || drops > 0 ? Tone::Warning : Tone::Accent,
                  extra,
                  colors);
    }

    // The switch and port this jack leads to.
    if (!info.carrier) {
        main_.set("SWITCH", "No link", Tone::Muted, "", colors);
    }
    else if (neighbor) {
        main_.set(join({"SWITCH", age_text(status.seconds_since_update)}),
                  or_none(neighbor->port_id),
                  Tone::Normal,
                  switch_name(*neighbor),
                  colors);
    }
    else if (status.state == State::Listening) {
        main_.set("SWITCH", "Listening...", Tone::Muted, "waiting for LLDP/CDP", colors);
    }
    else if (status.state == State::Unsupported) {
        main_.set("SWITCH", "n/a", Tone::Muted, "on the device only", colors);
    }
    else if (status.state == State::Error) {
        main_.set("SWITCH", "Error", Tone::Error, status.error, colors);
    }
    else {
        main_.set("SWITCH", kNone, Tone::Muted, "", colors);
    }
    lv_obj_set_style_text_color(main_.detail, colors.text, 0); // the switch name reads as a value

    // VLAN block: the number large, the VLAN name under it.
    const bool show_vlan = neighbor && neighbor->vlan >= 0;
    util::set_visible(vlan_.box, show_vlan);
    main_.reserve_right(show_vlan ? kVlanBlockWidth + kVlanBlockInset : 0);
    if (show_vlan) {
        lv_obj_set_style_bg_color(vlan_.box, colors.primary, 0);
        lv_obj_set_style_text_color(vlan_.caption, colors.primary, 0);
        lv_obj_set_style_text_color(vlan_.number, colors.primary, 0);
        lv_obj_set_style_text_color(vlan_.name, colors.text, 0);
        util::set_text(vlan_.number, std::to_string(neighbor->vlan));
        util::set_text(vlan_.name, neighbor->vlan_name);
        util::set_visible(vlan_.name, !neighbor->vlan_name.empty());
    }
}

void LinkScreen::refresh_wireless(const InterfaceInfo& info, const view::ThemePalette& colors) {
    const int32_t quality = info.has_signal ? platform::net::signal_quality_percent(info.signal_dbm) : -1;
    const Tone quality_tone = quality >= 50 ? Tone::Accent : quality >= 25 ? Tone::Warning : Tone::Error;

    if (!info.up) {
        main_.set("NETWORK", "Wi-Fi off", Tone::Muted, "interface off", colors);
    }
    else if (!info.carrier || info.ssid.empty()) {
        main_.set("NETWORK", "Not connected", Tone::Muted, "", colors);
    }
    else {
        main_.set("NETWORK",
                  info.ssid,
                  Tone::Normal,
                  join({"Wi-Fi", info.mtu > 0 ? "MTU " + std::to_string(info.mtu) : ""}),
                  colors);
    }

    if (quality < 0) {
        side_.set("SIGNAL", kNone, Tone::Muted, "", colors);
    }
    else {
        side_.set("SIGNAL", std::to_string(quality) + "%", quality_tone, std::to_string(info.signal_dbm) + " dBm", colors);
    }

    // One bar per quarter of signal quality.
    const int32_t lit = quality < 0 ? 0 : (quality + 24) / 25;
    for (size_t i = 0; i < signal_bars_.size(); ++i) {
        const bool on = static_cast<int32_t>(i) < lit;
        lv_obj_set_style_bg_color(signal_bars_[i], on ? bento::tone_color(quality_tone, colors) : colors.border, 0);
    }
}

void LinkScreen::refresh_addresses(const InterfaceInfo& info, const view::ThemePalette& colors) {
    auto& network = view_model().network();
    const auto& snapshot = network.snapshot();
    const auto report = network.test_report();

    // Address. A 169.254.x.x address means nothing handed one out.
    const std::string prefix = info.prefix_length >= 0 ? "/" + std::to_string(info.prefix_length) : "";
    if (info.ipv4.empty()) {
        address_.set("ADDRESS",
                     info.carrier ? "None" : kNone,
                     info.carrier ? Tone::Error : Tone::Muted,
                     info.carrier ? "no IPv4 address" : "",
                     colors);
    }
    else if (is_link_local_ipv4(info.ipv4)) {
        address_.set("ADDRESS", info.ipv4, Tone::Error, "self-assigned", colors);
    }
    else {
        address_.set("ADDRESS", info.ipv4, Tone::Normal, prefix, colors);
    }

    // Gateway: only meaningful on the interface that carries the default route.
    if (info.name != snapshot.gateway_interface || snapshot.gateway.empty()) {
        gateway_.set("GATEWAY", kNone, Tone::Muted, snapshot.gateway.empty() ? "no default route" : "other interface",
                     colors);
    }
    else {
        const auto ping = last_check(report, ConnectivityTester::GatewayPing);
        gateway_.set("GATEWAY", snapshot.gateway, Tone::Normal, ping.empty() ? "default route" : ping, colors);
    }

    // DNS servers are system-wide; show the first and how many more there are.
    const auto& servers = snapshot.dns_servers;
    if (servers.empty()) {
        dns_.set("DNS", kNone, Tone::Muted, "none configured", colors);
    }
    else {
        const auto more = servers.size() > 1 ? "+" + std::to_string(servers.size() - 1) : "";
        dns_.set("DNS", servers.front(), Tone::Normal, join({last_check(report, ConnectivityTester::Dns), more}), colors);
    }
}

Detail LinkScreen::detail(size_t tile) {
    const auto* info = view_model().network().selected_interface(kind_);
    if (!info) {
        Detail detail;
        detail.header = kind_ == viewmodel::LinkKind::Wired ? "ETHERNET" : "WI-FI";
        detail.value = "None";
        detail.suffix = "no adapter found";
        return detail;
    }
    switch (tile) {
        case 0:
            return kind_ == viewmodel::LinkKind::Wired ? switch_detail(*info) : wifi_detail(*info);
        case 1:
            return kind_ == viewmodel::LinkKind::Wired ? link_detail(*info) : wifi_detail(*info);
        case 2:
            return address_detail(*info);
        case 3:
            return gateway_detail(*info);
        default:
            return dns_detail();
    }
}

Detail LinkScreen::switch_detail(const InterfaceInfo& info) {
    using State = NeighborListener::State;
    const auto status = view_model().network().neighbor_status();
    if (status.state != State::Found || !status.neighbor) {
        Detail detail;
        detail.header = "SWITCH";
        detail.value = info.carrier ? "Not seen yet" : "No link";
        detail.value_tone = Tone::Muted;
        detail.rows = {
            {"Listens", "LLDP and CDP on " + info.name, Tone::Normal, true},
            {"Note", "switches announce every 30-60 s", Tone::Normal, true},
        };
        if (status.state == State::Error) {
            detail.rows.push_back({"Error", status.error, Tone::Error, true});
        }
        return detail;
    }

    const auto& neighbor = *status.neighbor;
    const char* protocol = neighbor.protocol == Neighbor::Protocol::Lldp ? "LLDP" : "CDP";
    Detail detail;
    detail.header = join({"SWITCH", protocol, age_text(status.seconds_since_update)});
    detail.value = or_none(neighbor.port_id);
    if (neighbor.port_description != neighbor.port_id) {
        detail.suffix = neighbor.port_description;
    }
    std::string poe = kNone;
    if (neighbor.poe_milliwatts > 0) {
        poe = watts_text(neighbor.poe_milliwatts);
    }
    if (neighbor.poe_class >= 0) {
        poe = join({poe == kNone ? "" : poe, "class " + std::to_string(neighbor.poe_class)});
    }
    detail.rows = {
        {"Switch", or_none(switch_name(neighbor))},
        {"VLAN", vlan_text(neighbor), neighbor.vlan >= 0 ? Tone::Accent : Tone::Normal},
        {"Model", or_none(model_text(neighbor))},
        {"Voice", neighbor.voice_vlan >= 0 ? "VLAN " + std::to_string(neighbor.voice_vlan) : kNone},
        {"Mgmt", or_none(neighbor.management_ip)},
        {"PoE", poe, poe == kNone ? Tone::Normal : Tone::Warning},
        {"Chassis", or_none(neighbor.chassis_id)},
        {"Link", info.carrier ? join({speed_text(info.speed_mbps), info.duplex}) : "down"},
    };
    return detail;
}

Detail LinkScreen::link_detail(const InterfaceInfo& info) {
    const auto drops = view_model().network().link_drops(info);
    Detail detail;
    detail.header = "LINK · " + info.name;
    if (!info.up) {
        detail.value = "Down";
        detail.value_tone = Tone::Muted;
    }
    else if (!info.carrier) {
        detail.value = "No cable";
        detail.value_tone = Tone::Error;
    }
    else {
        detail.value = speed_text(info.speed_mbps);
        detail.value_tone = Tone::Accent;
        detail.suffix = duplex_text(info);
        detail.suffix_tone = info.duplex == "half" ? Tone::Warning : Tone::Normal;
    }
    const auto status = view_model().network().neighbor_status();
    std::string poe = kNone;
    if (status.neighbor && !poe_text(*status.neighbor).empty()) {
        poe = poe_text(*status.neighbor).substr(4); // drop "PoE "
    }
    detail.rows = {
        {"Drops", drops < 0 ? kNone : std::to_string(drops) + " since start", drops > 0 ? Tone::Warning : Tone::Normal},
        {"MTU", info.mtu > 0 ? std::to_string(info.mtu) : kNone},
        {"PoE", poe},
        {"State", info.up ? (info.carrier ? "up" : "no carrier") : "down"},
        {"MAC", or_none(info.mac), Tone::Normal, true},
    };
    return detail;
}

Detail LinkScreen::wifi_detail(const InterfaceInfo& info) {
    const int32_t quality = info.has_signal ? platform::net::signal_quality_percent(info.signal_dbm) : -1;
    Detail detail;
    detail.header = "WI-FI · " + info.name;
    detail.value = info.ssid.empty() ? "Not connected" : info.ssid;
    detail.value_tone = info.ssid.empty() ? Tone::Muted : Tone::Normal;
    if (quality >= 0) {
        detail.suffix = std::to_string(quality) + "%";
        detail.suffix_tone = quality >= 50 ? Tone::Accent : quality >= 25 ? Tone::Warning : Tone::Error;
    }
    detail.rows = {
        {"Signal", info.has_signal ? std::to_string(info.signal_dbm) + " dBm" : kNone},
        {"MTU", info.mtu > 0 ? std::to_string(info.mtu) : kNone},
        {"State", info.up ? (info.carrier ? "connected" : "not connected") : "off"},
        {"MAC", or_none(info.mac)},
        {"IPv6", or_none(info.ipv6), Tone::Normal, true},
    };
    return detail;
}

Detail LinkScreen::address_detail(const InterfaceInfo& info) {
    Detail detail;
    detail.header = "ADDRESS · " + info.name;
    if (info.ipv4.empty()) {
        detail.value = "None";
        detail.value_tone = info.carrier ? Tone::Error : Tone::Muted;
        detail.suffix = info.carrier ? "no IPv4 address" : "no link";
    }
    else {
        detail.value = info.ipv4;
        detail.value_tone = is_link_local_ipv4(info.ipv4) ? Tone::Error : Tone::Normal;
        detail.suffix = is_link_local_ipv4(info.ipv4) ? "self-assigned, no DHCP"
                        : info.prefix_length >= 0     ? "/" + std::to_string(info.prefix_length)
                                                      : "";
    }
    const auto [mask, broadcast] = mask_and_broadcast(info.ipv4, info.prefix_length);
    detail.rows = {
        {"Mask", or_none(mask)},
        {"MAC", or_none(info.mac)},
        {"Bcast", or_none(broadcast)},
        {"MTU", info.mtu > 0 ? std::to_string(info.mtu) : kNone},
        {info.ipv6_link_local ? "IPv6 LL" : "IPv6", or_none(info.ipv6), Tone::Normal, true},
    };
    return detail;
}

Detail LinkScreen::gateway_detail(const InterfaceInfo& info) {
    auto& network = view_model().network();
    const auto& snapshot = network.snapshot();
    Detail detail;
    detail.header = "GATEWAY";
    if (snapshot.gateway.empty() || info.name != snapshot.gateway_interface) {
        detail.value = kNone;
        detail.value_tone = Tone::Muted;
        detail.suffix = snapshot.gateway.empty() ? "no default route" : "route goes via " + snapshot.gateway_interface;
        return detail;
    }
    detail.header = "GATEWAY · default route via " + info.name;
    detail.value = snapshot.gateway;
    const auto report = network.test_report();
    const auto& ping = report.steps[ConnectivityTester::GatewayPing];
    const bool checked = report.runs > 0 && !report.running;
    detail.rows = {
        {"MAC", or_none(platform::net::neighbor_mac(snapshot.gateway)), Tone::Normal, true},
        {"Ping", checked ? or_none(ping.headline) : "run the checks (6)"},
        {"Loss", checked ? or_none(ping.detail) : kNone},
    };
    return detail;
}

Detail LinkScreen::dns_detail() {
    auto& network = view_model().network();
    const auto& servers = network.snapshot().dns_servers;
    const auto report = network.test_report();
    Detail detail;
    detail.header = "DNS · " + std::to_string(servers.size()) + (servers.size() == 1 ? " server" : " servers");
    detail.value = servers.empty() ? "None" : servers.front();
    detail.value_tone = servers.empty() ? Tone::Error : Tone::Normal;
    // One wide row per server with its result from the last check.
    for (size_t i = 0; i < servers.size() && i < 4; ++i) {
        std::string result;
        for (const auto& [server, outcome] : report.dns_results) {
            if (server == servers[i]) {
                result = outcome;
            }
        }
        const bool failed = !result.empty() && result.find("ms") == std::string::npos;
        detail.rows.push_back({std::to_string(i + 1) + ".",
                               join({servers[i], result}),
                               failed ? Tone::Error : Tone::Normal,
                               true});
    }
    if (report.dns_results.empty()) {
        detail.suffix = "run the checks (6)";
    }
    return detail;
}

} // namespace screen
