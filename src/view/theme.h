/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

namespace view {

constexpr int kScreenWidth    = 320;
constexpr int kScreenHeight   = 170;
constexpr int kTitleBarHeight = 20;
constexpr int kNavBarHeight   = 24;

struct ThemePalette {
    lv_color_t background;     // page, content area and both bars
    lv_color_t surface;        // same as background: content sits on the page
    lv_color_t bar;
    lv_color_t tile;           // bento tiles
    lv_color_t button;
    lv_color_t border;
    lv_color_t text;           // values
    lv_color_t text_secondary; // detail lines under a value
    lv_color_t text_muted;     // tile labels
    lv_color_t text_disabled;  // unavailable values and disabled icons
    lv_color_t primary;
    lv_color_t info;
    lv_color_t success;
    lv_color_t warning;
    lv_color_t error;
};

ThemePalette palette(bool dark_mode);
void apply_lvgl_theme(lv_display_t* display, bool dark_mode, const lv_font_t* font = LV_FONT_DEFAULT);

} // namespace view
