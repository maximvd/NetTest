/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstring>
#include <string>

namespace screen::util {

// lv_label_set_text restarts scrolling animations, so only touch labels whose
// text actually changed (screens refresh every second).
inline void set_text(lv_obj_t* label, const char* text) {
    if (label && std::strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

inline void set_text(lv_obj_t* label, const std::string& text) {
    set_text(label, text.c_str());
}

inline void set_visible(lv_obj_t* obj, bool visible) {
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

inline lv_obj_t* create_label(lv_obj_t* parent,
                              const lv_font_t* font,
                              int32_t x,
                              int32_t y,
                              int32_t width,
                              lv_label_long_mode_t long_mode = LV_LABEL_LONG_MODE_DOTS) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, long_mode);
    lv_obj_set_pos(label, x, y);
    return label;
}

} // namespace screen::util
