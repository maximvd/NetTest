/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "tests_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "screen_utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>

namespace screen {
namespace {

using bento::Detail;
using bento::Tone;
using Tester = platform::net::ConnectivityTester;
using util::join;

struct Verdict {
    std::string value;
    Tone tone;
    std::string detail;
};

Tone step_tone(Tester::StepState state) {
    switch (state) {
        case Tester::StepState::Ok:
            return Tone::Normal;
        case Tester::StepState::Warn:
            return Tone::Warning;
        case Tester::StepState::Fail:
            return Tone::Error;
        case Tester::StepState::Running:
        case Tester::StepState::Skipped:
        case Tester::StepState::Idle:
        default:
            return Tone::Muted;
    }
}

// The 1.1.1.1 tile: ping, or the TCP 443 connect when the network blocks ping.
Tester::StepResult internet_result(const Tester::Report& report) {
    const auto& ping = report.steps[Tester::InternetPing];
    const auto& tcp = report.steps[Tester::InternetTcp];
    if (ping.state == Tester::StepState::Fail && tcp.state == Tester::StepState::Ok) {
        return {Tester::StepState::Ok, tcp.headline, "ping blocked, TCP 443 ok"};
    }
    return ping;
}

Tester::StepResult shown_result(const Tester::Report& report, Tester::Step step) {
    return step == Tester::InternetPing ? internet_result(report) : report.steps[step];
}

Verdict verdict(const Tester::Report& report, const std::array<Tester::Step, 4>& shown) {
    const auto& steps = report.steps;
    if (report.running) {
        for (const auto& step : steps) {
            if (step.state == Tester::StepState::Running) {
                return {"Checking...", Tone::Muted, step.detail};
            }
        }
        return {"Checking...", Tone::Muted, ""};
    }
    if (report.runs == 0) {
        return {"Not run yet", Tone::Muted, "press 7 to run the checks"};
    }

    size_t passed = 0;
    for (const auto step : shown) {
        passed += shown_result(report, step).state == Tester::StepState::Ok ? 1 : 0;
    }
    const auto passed_text = std::to_string(passed) + " of " + std::to_string(shown.size()) + " checks passed";

    const auto internet = steps[Tester::InternetTcp].state;
    const auto dns = steps[Tester::Dns].state;
    const auto gateway = steps[Tester::GatewayPing].state;
    const auto dhcp = steps[Tester::Dhcp].state;
    const bool internet_ok = internet == Tester::StepState::Ok;
    if (internet_ok && dns == Tester::StepState::Ok) {
        if (dhcp == Tester::StepState::Warn) {
            return {"Online", Tone::Warning, "2+ DHCP servers: rogue?"};
        }
        return {"Online", Tone::Accent, passed_text};
    }
    if (internet_ok && dns == Tester::StepState::Warn) {
        return {"Online", Tone::Warning, "a DNS server is not answering"};
    }
    if (internet_ok) {
        return {"DNS failing", Tone::Error, "internet works, names don't resolve"};
    }
    if (gateway == Tester::StepState::Ok || gateway == Tester::StepState::Warn) {
        return {"No internet", Tone::Error, "gateway answers, internet doesn't"};
    }
    return {"Offline", Tone::Error, "no gateway or internet: check the link"};
}

std::string run_age(const Tester::Report& report) {
    if (report.running || report.runs == 0) {
        return "";
    }
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - report.finished_at).count();
    return util::age_text(seconds);
}

std::string lease_text(int32_t seconds) {
    if (seconds <= 0) {
        return "-";
    }
    if (seconds >= 86400) {
        const auto days = seconds / 86400;
        return std::to_string(days) + (days == 1 ? " day" : " days");
    }
    if (seconds >= 3600) {
        return std::to_string(seconds / 3600) + " h";
    }
    return std::to_string(seconds / 60) + " min";
}

std::string list_text(const std::vector<std::string>& items) {
    std::string text;
    for (const auto& item : items) {
        text += (text.empty() ? "" : ", ") + item;
    }
    return text.empty() ? "-" : text;
}

} // namespace

TestsScreen::TestsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BentoScreen(view_model, assets) {
    init();
}

void TestsScreen::build_content(lv_obj_t* content) {
    const auto fonts = bento::Fonts::load(assets());
    verdict_ = bento::create_tile(content, fonts, {0, 0, 2}, bento::ValueSize::Large);
    constexpr std::array<bento::Cell, kTileSteps.size()> kCells = {{{2, 0}, {0, 1}, {1, 1}, {2, 1}}};
    for (size_t i = 0; i < steps_.size(); ++i) {
        steps_[i] = bento::create_tile(content, fonts, kCells[i], bento::ValueSize::Large);
    }

    attach_tiles(content, fonts, {&verdict_, &steps_[0], &steps_[1], &steps_[2], &steps_[3]});
    auto& network = view_model().network();
    reactive::observe_obj(content, network.tests_revision_subject(), changed_cb, this);
    // Ticks every second, which keeps "N s ago" current.
    reactive::observe_obj(content, network.link_revision_subject(), changed_cb, this);
}

void TestsScreen::refresh_tiles() {
    const auto report = view_model().network().test_report();
    const auto colors = view::palette(view_model().is_dark_mode());

    const auto result = verdict(report, kTileSteps);
    verdict_.set(join({"VERDICT", run_age(report)}), result.value, result.tone, result.detail, colors);

    for (size_t i = 0; i < steps_.size(); ++i) {
        const auto step = kTileSteps[i];
        const auto shown = shown_result(report, step);
        const bool idle = shown.state == Tester::StepState::Idle;
        steps_[i].set(Tester::step_name(step),
                      idle ? "-" : shown.headline,
                      step_tone(shown.state),
                      idle ? "" : shown.detail,
                      colors);
    }
}

Detail TestsScreen::detail(size_t tile) {
    const auto report = view_model().network().test_report();
    if (tile == 0) {
        return verdict_detail(report);
    }
    const auto step = kTileSteps[std::min(tile - 1, kTileSteps.size() - 1)];
    return step == Tester::Dhcp ? dhcp_detail(report) : step_detail(report, step);
}

Detail TestsScreen::verdict_detail(const Tester::Report& report) {
    const auto result = verdict(report, kTileSteps);
    Detail detail;
    detail.header = join({"VERDICT", run_age(report), "7 runs again"});
    detail.value = result.value;
    detail.value_tone = result.tone;
    detail.suffix = result.detail;
    const auto row = [&](const char* key, Tester::Step step) {
        const auto& shown = report.steps[step];
        return bento::DetailRow{key, shown.headline.empty() ? "-" : shown.headline, step_tone(shown.state)};
    };
    detail.rows = {
        row("DHCP", Tester::Dhcp),
        row("Gateway", Tester::GatewayPing),
        row("Ping", Tester::InternetPing),
        row("TCP 443", Tester::InternetTcp),
        row("DNS", Tester::Dns),
        {"Via", report.dhcp_interface.empty() ? "-" : report.dhcp_interface},
    };
    return detail;
}

Detail TestsScreen::dhcp_detail(const Tester::Report& report) {
    const auto& step = report.steps[Tester::Dhcp];
    const auto& offers = report.dhcp_offers;
    Detail detail;
    detail.header = "DHCP";
    if (!report.dhcp_interface.empty()) {
        detail.header += " · asked on " + report.dhcp_interface;
    }
    if (!offers.empty() && offers.front().response_ms >= 0) {
        detail.header += " · answer in " + std::to_string(offers.front().response_ms) + " ms";
    }
    detail.value = step.headline.empty() ? "-" : step.headline;
    detail.value_tone = step_tone(step.state);

    if (offers.empty()) {
        detail.suffix = step.detail;
        detail.rows = {{"How", "one DISCOVER, offers collected for 2.5 s; no address is taken", Tone::Normal, true}};
        return detail;
    }
    if (offers.size() == 1) {
        const auto& offer = offers.front();
        detail.suffix = "no rogue servers";
        detail.suffix_tone = Tone::Accent;
        detail.rows = {
            {"Server", offer.server},
            {"Offers", offer.offered_ip},
            {"Router", offer.router.empty() ? "-" : offer.router},
            {"Lease", lease_text(offer.lease_seconds)},
            {"DNS", list_text(offer.dns_servers), Tone::Normal, true},
            {"Domain", offer.domain.empty() ? "-" : offer.domain, Tone::Normal, true},
        };
        return detail;
    }
    // Several servers answered: list each with what it offers.
    detail.suffix = "rogue or misconfigured server?";
    detail.suffix_tone = Tone::Warning;
    for (size_t i = 0; i < offers.size() && i < 4; ++i) {
        const auto& offer = offers[i];
        detail.rows.push_back({std::to_string(i + 1) + ".",
                               offer.server + " offers " + offer.offered_ip +
                                   (offer.router.empty() ? "" : ", router " + offer.router),
                               Tone::Normal,
                               true});
    }
    return detail;
}

Detail TestsScreen::step_detail(const Tester::Report& report, Tester::Step step) {
    const auto shown = shown_result(report, step);
    Detail detail;
    detail.value = shown.headline.empty() ? "-" : shown.headline;
    detail.value_tone = step_tone(shown.state);
    detail.suffix = shown.detail;

    const auto row_for = [&](const char* key, Tester::Step source) {
        const auto& result = report.steps[source];
        return bento::DetailRow{key,
                                result.headline.empty() ? "-" : join({result.headline, result.detail}),
                                step_tone(result.state),
                                true};
    };
    const auto& gateway = view_model().network().snapshot().gateway;
    switch (step) {
        case Tester::GatewayPing:
            detail.header = "GATEWAY · 4 pings, 250 ms apart";
            detail.rows = {{"Target", gateway.empty() ? "-" : gateway, Tone::Normal, true}};
            break;
        case Tester::InternetPing:
            detail.header = std::string("INTERNET · ") + Tester::kInternetPingTarget;
            detail.rows = {row_for("Ping", Tester::InternetPing), row_for("TCP 443", Tester::InternetTcp)};
            break;
        case Tester::Dns:
        default:
            detail.header = std::string("DNS · resolving ") + Tester::kDnsTestHost;
            for (size_t i = 0; i < report.dns_results.size() && i < 4; ++i) {
                const auto& [server, outcome] = report.dns_results[i];
                const bool failed = outcome.find("ms") == std::string::npos;
                detail.rows.push_back(
                    {std::to_string(i + 1) + ".", server + " · " + outcome, failed ? Tone::Error : Tone::Normal, true});
            }
            break;
    }
    return detail;
}

} // namespace screen
