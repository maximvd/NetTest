/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "screen_utils.h"

#include <string>
#include <vector>

namespace screen {
namespace {

using bento::Tone;
using platform::net::ConnectivityTester;
using platform::net::InterfaceInfo;
using platform::net::Neighbor;
using platform::net::NeighborListener;

constexpr const char* kNone = "-";
constexpr int32_t kBadgeReserve = 54;
constexpr int32_t kBarWidth = 5;
constexpr int32_t kBarGap = 2;
constexpr int32_t kBarMaxHeight = 20;

std::string join(const std::vector<std::string>& parts) {
    std::string text;
    for (const auto& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += " · ";
        }
        text += part;
    }
    return text;
}

std::string age_text(int32_t seconds) {
    if (seconds < 0) {
        return "";
    }
    return seconds < 120 ? std::to_string(seconds) + "s ago" : std::to_string(seconds / 60) + "m ago";
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

std::string vlan_badge_text(const Neighbor& neighbor) {
    return neighbor.vlan >= 0 ? "VLAN " + std::to_string(neighbor.vlan) : "";
}

// Shown under the port: the switch, then whatever the port is called.
std::string switch_detail(const Neighbor& neighbor) {
    const auto& name = neighbor.system_name.empty() ? neighbor.chassis_id : neighbor.system_name;
    std::string extra;
    if (!neighbor.port_description.empty() && neighbor.port_description != neighbor.port_id) {
        extra = neighbor.port_description;
    }
    else if (neighbor.vlan < 0 && !neighbor.vlan_name.empty()) {
        extra = neighbor.vlan_name;
    }
    else {
        extra = neighbor.management_ip;
    }
    return join({name, extra});
}

// The PoE power the switch gives this port, e.g. "PoE 15.4 W" or "PoE class 3".
std::string poe_text(const Neighbor& neighbor) {
    if (neighbor.poe_milliwatts > 0) {
        const auto tenths = (neighbor.poe_milliwatts + 50) / 100;
        const auto watts = std::to_string(tenths / 10) + (tenths % 10 ? "." + std::to_string(tenths % 10) : "");
        return "PoE " + watts + " W";
    }
    return neighbor.poe_class >= 0 ? "PoE class " + std::to_string(neighbor.poe_class) : "";
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

} // namespace

LinkScreen::LinkScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       viewmodel::LinkKind kind)
    : BaseScreen(view_model, assets), kind_(kind) {
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
        vlan_badge_ = bento::create_badge(main_, fonts);
    }
    else {
        // Signal bars in the network tile's top-right corner, short to tall.
        main_.reserve_right(kSignalBars * (kBarWidth + kBarGap) + 4);
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

    empty_label_ = lv_label_create(content);
    lv_obj_set_style_text_font(empty_label_, fonts.value_medium, 0);
    lv_label_set_text(empty_label_, kind_ == viewmodel::LinkKind::Wired ? "No Ethernet port" : "No Wi-Fi adapter");
    lv_obj_center(empty_label_);

    reactive::observe_obj(content, view_model().network().link_revision_subject(), link_changed_cb, this);
    reactive::observe_obj(content, view_model().network().tests_revision_subject(), link_changed_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), link_changed_cb, this);
}

void LinkScreen::show_tiles(bool visible) {
    for (const auto* tile : {&main_, &side_, &address_, &gateway_, &dns_}) {
        util::set_visible(tile->box, visible);
    }
    util::set_visible(empty_label_, !visible);
}

void LinkScreen::refresh() {
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

    // Link speed, or why there is no link.
    if (!info.up) {
        side_.set("LINK", "Down", Tone::Muted, "interface disabled", colors);
    }
    else if (!info.carrier) {
        side_.set("LINK", "No cable", Tone::Error, "plug in a cable", colors);
    }
    else {
        // Drops since NetTest started point at a bad cable or port; otherwise show the MTU.
        const auto drops = view_model().network().link_drops(info);
        const std::string duplex = info.duplex == "half" ? "HALF duplex" : info.duplex;
        std::string extra = info.mtu > 0 ? "MTU " + std::to_string(info.mtu) : "";
        if (drops > 0) {
            extra = std::to_string(drops) + (drops == 1 ? " drop" : " drops");
        }
        side_.set("LINK",
                  speed_text(info.speed_mbps),
                  info.duplex == "half" || drops > 0 ? Tone::Warning : Tone::Accent,
                  join({duplex, extra}),
                  colors);
    }

    // The switch and port this jack leads to.
    const auto status = view_model().network().neighbor_status();
    std::string badge;
    if (!info.carrier) {
        main_.set("SWITCH", "No link", Tone::Muted, "switch shows up once linked", colors);
    }
    else if (status.state == State::Found && status.neighbor) {
        const auto& neighbor = *status.neighbor;
        const char* protocol = neighbor.protocol == Neighbor::Protocol::Lldp ? "LLDP" : "CDP";
        main_.set(join({"SWITCH", protocol, age_text(status.seconds_since_update), poe_text(neighbor)}),
                  neighbor.port_id.empty() ? kNone : neighbor.port_id,
                  Tone::Normal,
                  switch_detail(neighbor),
                  colors);
        badge = vlan_badge_text(neighbor);
    }
    else if (status.state == State::Listening) {
        const auto waited = status.seconds_since_update >= 0 ? std::to_string(status.seconds_since_update) + "s" : "";
        main_.set("SWITCH", "Listening...", Tone::Muted, join({"waiting for LLDP/CDP", waited}), colors);
    }
    else if (status.state == State::Unsupported) {
        main_.set("SWITCH", "n/a", Tone::Muted, "discovery runs on the device", colors);
    }
    else if (status.state == State::Error) {
        main_.set("SWITCH", "Error", Tone::Error, status.error, colors);
    }
    else {
        main_.set("SWITCH", kNone, Tone::Muted, "", colors);
    }

    bento::set_badge(vlan_badge_, badge, colors.primary, colors.background);
    main_.reserve_right(badge.empty() ? 0 : kBadgeReserve);
}

void LinkScreen::refresh_wireless(const InterfaceInfo& info, const view::ThemePalette& colors) {
    const int32_t quality = info.has_signal ? platform::net::signal_quality_percent(info.signal_dbm) : -1;
    const Tone quality_tone = quality >= 50 ? Tone::Accent : quality >= 25 ? Tone::Warning : Tone::Error;

    if (!info.up) {
        main_.set("NETWORK", "Wi-Fi off", Tone::Muted, "interface disabled", colors);
    }
    else if (!info.carrier || info.ssid.empty()) {
        main_.set("NETWORK", "Not connected", Tone::Muted, "", colors);
    }
    else {
        main_.set("NETWORK", info.ssid, Tone::Normal, join({"Wi-Fi", info.mtu > 0 ? "MTU " + std::to_string(info.mtu) : ""}),
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
        address_.set("ADDRESS", info.ipv4, Tone::Error, "self-assigned: no DHCP", colors);
    }
    else {
        address_.set("ADDRESS", info.ipv4, Tone::Normal, prefix.empty() ? "" : "subnet " + prefix, colors);
    }

    // Gateway: only meaningful on the interface that carries the default route.
    if (info.name != snapshot.gateway_interface || snapshot.gateway.empty()) {
        gateway_.set("GATEWAY", kNone, Tone::Muted, snapshot.gateway.empty() ? "no default route" : "not default route",
                     colors);
    }
    else {
        const auto ping = last_check(report, ConnectivityTester::GatewayPing);
        gateway_.set("GATEWAY", snapshot.gateway, Tone::Normal, ping.empty() ? "default route" : ping + " ping", colors);
    }

    // DNS servers are system-wide; show the first and how many more there are.
    const auto& servers = snapshot.dns_servers;
    if (servers.empty()) {
        dns_.set("DNS", kNone, Tone::Muted, "none configured", colors);
    }
    else {
        const auto more = servers.size() > 1 ? "+" + std::to_string(servers.size() - 1) + " more" : "";
        dns_.set("DNS", servers.front(), Tone::Normal, join({last_check(report, ConnectivityTester::Dns), more}), colors);
    }
}

void LinkScreen::link_changed_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<LinkScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
