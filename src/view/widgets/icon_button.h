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

// Transparent button showing one icon glyph. The glyph uses the theme's text
// colour, the accent colour when active (current page), or the disabled colour.
class IconButton : public BaseWidgets {
public:
    IconButton(lv_obj_t* parent,
               viewmodel::BaseViewModel& view_model,
               int32_t width,
               int32_t height,
               const lv_font_t* font);

    void build() override;
    void set_text(const char* text);
    void set_enabled(bool enabled);
    void set_active(bool active);

private:
    static void theme_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    void apply_style();

    viewmodel::BaseViewModel& view_model_;
    int32_t width_;
    int32_t height_;
    std::string text_;
    const lv_font_t* font_;
    lv_obj_t* label_{nullptr};
    bool enabled_{true};
    bool active_{false};
};

} // namespace view::widgets
