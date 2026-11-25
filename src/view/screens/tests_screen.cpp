/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "tests_screen.h"

#include "asset_manager.h"
#include "bindings.h"
#include "screen_utils.h"
#include "theme.h"

#include <string>

namespace screen {
namespace {

using Tester = platform::net::ConnectivityTester;

constexpr int32_t kPadX = 10;
constexpr int32_t kRowsY = 6;
constexpr int32_t kRowPitch = 21;
constexpr int32_t kDotSize = 9;
constexpr int32_t kNameX = kPadX + kDotSize + 8;
constexpr int32_t kNameWidth = 92;
constexpr int32_t kValueX = kNameX + kNameWidth;

lv_color_t state_color(Tester::StepState state, const view::ThemePalette& colors) {
    switch (state) {
        case Tester::StepState::Ok:
            return colors.success;
        case Tester::StepState::Warn:
            return colors.warning;
        case Tester::StepState::Fail:
            return colors.error;
        case Tester::StepState::Running:
            return colors.info;
        case Tester::StepState::Skipped:
        case Tester::StepState::Idle:
        default:
            return colors.text_disabled;
    }
}

std::string summary_text(const Tester::Report& report) {
    if (report.running) {
        return "Running checks...";
    }
    if (report.runs == 0) {
        return "Press 7 to run the checks";
    }
    const auto& internet = report.steps[Tester::InternetTcp];
    const auto& dns = report.steps[Tester::Dns];
    const auto& gateway = report.steps[Tester::GatewayPing];
    if (internet.state == Tester::StepState::Ok && dns.state == Tester::StepState::Ok) {
        return "Online. Press 7 to run again";
    }
    if (internet.state == Tester::StepState::Ok && dns.state == Tester::StepState::Warn) {
        return "Online, but a DNS server is not answering";
    }
    if (internet.state == Tester::StepState::Ok) {
        return "Internet OK but DNS is failing";
    }
    if (gateway.state == Tester::StepState::Ok || gateway.state == Tester::StepState::Warn) {
        return "Gateway OK, no internet access";
    }
    return "No connectivity. Check the link";
}

} // namespace

TestsScreen::TestsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void TestsScreen::build_content(lv_obj_t* content) {
    const lv_font_t* name_font = assets().load_standard_font(12);
    const lv_font_t* value_font = assets().load_standard_font(12, app::StandardFontWeight::Regular);
    name_font = name_font ? name_font : &lv_font_montserrat_12;
    value_font = value_font ? value_font : &lv_font_montserrat_12;
    auto* dark_mode = view_model().dark_mode_subject();

    for (size_t i = 0; i < rows_.size(); ++i) {
        const auto y = kRowsY + static_cast<int32_t>(i) * kRowPitch;
        auto& row = rows_[i];

        row.dot = lv_obj_create(content);
        lv_obj_remove_style_all(row.dot);
        lv_obj_set_size(row.dot, kDotSize, kDotSize);
        lv_obj_set_pos(row.dot, kPadX, y + 4);
        lv_obj_set_style_radius(row.dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(row.dot, LV_OPA_COVER, 0);

        row.name = util::create_label(content, name_font, kNameX, y, kNameWidth);
        lv_label_set_text(row.name, Tester::step_name(static_cast<Tester::Step>(i)));
        reactive::bind_theme(row.name, dark_mode, reactive::ThemeRole::Text);

        row.value = util::create_label(content,
                                       value_font,
                                       kValueX,
                                       y,
                                       view::kScreenWidth - kValueX - kPadX,
                                       LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        reactive::bind_theme(row.value, dark_mode, reactive::ThemeRole::Text);
    }

    summary_label_ = util::create_label(content,
                                        name_font,
                                        kPadX,
                                        kRowsY + static_cast<int32_t>(rows_.size()) * kRowPitch + 4,
                                        view::kScreenWidth - 2 * kPadX);

    reactive::observe_obj(content, view_model().network().tests_revision_subject(), tests_changed_cb, this);
    reactive::observe_obj(content, dark_mode, tests_changed_cb, this);
}

void TestsScreen::refresh() {
    const auto report = view_model().network().test_report();
    const auto colors = view::palette(view_model().is_dark_mode());

    for (size_t i = 0; i < rows_.size(); ++i) {
        const auto& step = report.steps[i];
        lv_obj_set_style_bg_color(rows_[i].dot, state_color(step.state, colors), 0);
        util::set_text(rows_[i].value, step.state == Tester::StepState::Idle && !report.running ? "-" : step.value);
    }

    util::set_text(summary_label_, summary_text(report));
    const bool all_good = !report.running && report.runs > 0 &&
                          report.steps[Tester::InternetTcp].state == Tester::StepState::Ok &&
                          report.steps[Tester::Dns].state == Tester::StepState::Ok;
    lv_obj_set_style_text_color(summary_label_,
                                report.running || report.runs == 0 ? colors.text_disabled
                                : all_good                         ? colors.success
                                                                   : colors.warning,
                                0);
}

void TestsScreen::tests_changed_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<TestsScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
