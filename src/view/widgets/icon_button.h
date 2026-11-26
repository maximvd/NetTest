/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"

#include <cstdint>
#include <string>

namespace view::widgets {

// Transparent button showing one outline icon (a white PNG, tinted). The icon
// uses the theme's text colour, the accent colour when active (current page),
// or the disabled colour.
class IconButton : public BaseWidgets {
public:
    IconButton(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, int32_t width, int32_t height);

    void build() override;
    // LVGL image source ("A:/usr/share/nettest/images/nav_wifi.png"); empty hides the icon.
    void set_icon(const std::string& source);
    void set_enabled(bool enabled);
    void set_active(bool active);

private:
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    void apply_style();

    viewmodel::BaseViewModel& view_model_;
    int32_t width_;
    int32_t height_;
    std::string source_;
    lv_obj_t* image_{nullptr};
    bool enabled_{true};
    bool active_{false};
};

} // namespace view::widgets
