/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "navbar.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"
#include "ui_const.h"

namespace view::widgets {
namespace {

constexpr int32_t kButtonWidth = 32;
constexpr int32_t kButtonHeight = 20;
constexpr int32_t kIconSize = 18;
// Nudges the evenly spaced buttons so they sit above the matching keys.
constexpr std::array<int32_t, 5> kNavButtonXOffsets = {30, 17, 2, -15, -30};

} // namespace

NavBar::NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

NavBar::~NavBar() {
    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        if (icon_buttons_[i]) {
            platform::unregister_nav_button(i, icon_buttons_[i]->root());
        }
    }
    // The bar's objects outlive this widget briefly (the screen deletes its
    // root afterwards), so detach the observers that point back at `this`.
    if (page_observer_) {
        lv_observer_remove(page_observer_);
    }
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
    }
    if (link_observer_) {
        lv_observer_remove(link_observer_);
    }
}

void NavBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(core_obj_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(core_obj_, 8, 0);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    create_icon_buttons();
    page_observer_ = reactive::observe_obj(core_obj_, view_model_.current_page_subject(), update_icons_cb, this);
    theme_observer_ = reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), update_icons_cb, this);
    link_observer_ =
        reactive::observe_obj(core_obj_, view_model_.network().link_revision_subject(), update_icons_cb, this);
}

void NavBar::create_icon_buttons() {
    const lv_font_t* icon_font = assets_.load_font("Phosphor-Fill.ttf", kIconSize);
    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        auto& button = icon_buttons_[i];
        button = std::make_unique<IconButton>(core_obj_,
                                              view_model_,
                                              kButtonWidth,
                                              kButtonHeight,
                                              icon_font ? icon_font : &lv_font_montserrat_14);
        button->build();
        lv_obj_set_style_translate_x(button->root(), kNavButtonXOffsets[i], 0);
        lv_obj_add_event_cb(button->root(), clicked_cb, LV_EVENT_CLICKED, this);
        platform::register_nav_button(i, button->root());
    }
}

void NavBar::update_icon_buttons() {
    const auto page = view_model_.current_page();
    const bool tests_page = page == model::AppPage::Tests;
    const bool can_switch_interface =
        !tests_page && view_model_.network().interfaces(view_model_.current_link_kind()).size() > 1;

    struct Spec {
        const char* icon;
        Action action;
        bool active;
    };
    const std::array<Spec, kButtonCount> specs = {{
        {ICON_ETHERNET, Action::ShowEthernet, page == model::AppPage::Ethernet},
        {ICON_WIFI_HIGH, Action::ShowWifi, page == model::AppPage::Wifi},
        {ICON_HEARTBEAT, Action::ShowTests, tests_page},
        tests_page             ? Spec{ICON_PLAY, Action::RunTests, false}
        : can_switch_interface ? Spec{ICON_ARROWS_DOWN_UP, Action::NextInterface, false}
                               : Spec{ICON_ARROWS_DOWN_UP, Action::None, false},
        {view_model_.is_dark_mode() ? ICON_SUN : ICON_MOON, Action::ToggleTheme, false},
    }};

    for (size_t i = 0; i < icon_buttons_.size(); ++i) {
        actions_[i] = specs[i].action;
        icon_buttons_[i]->set_text(specs[i].icon);
        icon_buttons_[i]->set_active(specs[i].active);
        icon_buttons_[i]->set_enabled(specs[i].action != Action::None);
    }
}

void NavBar::perform(Action action) {
    switch (action) {
        case Action::None:
            break;
        case Action::ShowEthernet:
            view_model_.show_ethernet_page();
            break;
        case Action::ShowWifi:
            view_model_.show_wifi_page();
            break;
        case Action::ShowTests:
            view_model_.show_tests_page();
            break;
        case Action::NextInterface:
            view_model_.network().select_adjacent_interface(view_model_.current_link_kind(), +1);
            break;
        case Action::RunTests:
            view_model_.network().run_tests();
            break;
        case Action::ToggleTheme:
            view_model_.toggle_dark_mode();
            break;
    }
}

void NavBar::clicked_cb(lv_event_t* event) {
    auto* nav_bar = static_cast<NavBar*>(lv_event_get_user_data(event));
    auto* target = lv_event_get_current_target(event);
    if (!nav_bar) {
        return;
    }
    for (size_t i = 0; i < nav_bar->icon_buttons_.size(); ++i) {
        if (nav_bar->icon_buttons_[i] && nav_bar->icon_buttons_[i]->root() == target) {
            nav_bar->perform(nav_bar->actions_[i]);
            return;
        }
    }
}

void NavBar::update_icons_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* nav_bar = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (nav_bar) {
        nav_bar->update_icon_buttons();
    }
}

} // namespace view::widgets
