/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"
#include "icon_button.h"

#include <array>
#include <memory>
#include <string>

namespace app {
class AssetManager;
}

namespace view::widgets {

// Bottom bar with five button slots, triggered by keys 4..8 (left to right):
// Ethernet page, Wi-Fi page, Tests page, a page action (next interface on the
// link pages, run checks on Tests) and the light/dark toggle. The current
// page's icon is drawn in the accent colour.
class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~NavBar() override;

    void build() override;

private:
    enum class Action { None, ShowEthernet, ShowWifi, ShowTests, NextInterface, RunTests, ToggleTheme };

    static constexpr size_t kButtonCount = 5;

    void create_icon_buttons();
    void update_icon_buttons();
    std::string icon(const char* name) const;
    void perform(Action action);

    static void clicked_cb(lv_event_t* event);
    static void update_icons_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    std::array<std::unique_ptr<IconButton>, kButtonCount> icon_buttons_;
    std::array<Action, kButtonCount> actions_{};
    lv_observer_t* page_observer_{nullptr};
    lv_observer_t* theme_observer_{nullptr};
    lv_observer_t* link_observer_{nullptr}; // interface count enables "next interface"
};

} // namespace view::widgets
