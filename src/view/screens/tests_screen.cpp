/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "tests_screen.h"

#include "asset_manager.h"
#include "bindings.h"

#include <array>
#include <chrono>
#include <string>

namespace screen {
namespace {

using bento::Tone;
using Tester = platform::net::ConnectivityTester;

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

Verdict verdict(const Tester::Report& report) {
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

    constexpr std::array<Tester::Step, 4> kShown = {Tester::Dhcp, Tester::GatewayPing, Tester::InternetPing, Tester::Dns};
    size_t passed = 0;
    for (const auto step : kShown) {
        passed += shown_result(report, step).state == Tester::StepState::Ok ? 1 : 0;
    }
    const auto passed_text = std::to_string(passed) + " of " + std::to_string(kShown.size()) + " checks passed";

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

std::string verdict_label(const Tester::Report& report) {
    if (report.running || report.runs == 0) {
        return "VERDICT";
    }
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                                          report.finished_at)
                             .count();
    const auto age = seconds < 120 ? std::to_string(seconds) + "s ago" : std::to_string(seconds / 60) + "m ago";
    return "VERDICT · " + age;
}

} // namespace

TestsScreen::TestsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void TestsScreen::build_content(lv_obj_t* content) {
    const auto fonts = bento::Fonts::load(assets());
    verdict_ = bento::create_tile(content, fonts, {0, 0, 2}, bento::ValueSize::Large);
    constexpr std::array<bento::Cell, 4> kCells = {{{2, 0}, {0, 1}, {1, 1}, {2, 1}}};
    for (size_t i = 0; i < steps_.size(); ++i) {
        steps_[i].tile = bento::create_tile(content, fonts, kCells[i], bento::ValueSize::Large);
    }

    reactive::observe_obj(content, view_model().network().tests_revision_subject(), tests_changed_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), tests_changed_cb, this);
    // Ticks every second, which keeps "N s ago" current.
    reactive::observe_obj(content, view_model().network().link_revision_subject(), tests_changed_cb, this);
}

void TestsScreen::refresh() {
    const auto report = view_model().network().test_report();
    const auto colors = view::palette(view_model().is_dark_mode());

    const auto result = verdict(report);
    verdict_.set(verdict_label(report), result.value, result.tone, result.detail, colors);

    for (auto& [step, tile] : steps_) {
        const auto result = shown_result(report, step);
        const bool idle = result.state == Tester::StepState::Idle;
        tile.set(Tester::step_name(step),
                 idle ? "-" : result.headline,
                 step_tone(result.state),
                 idle ? "" : result.detail,
                 colors);
    }
}

void TestsScreen::tests_changed_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<TestsScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
