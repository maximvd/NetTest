/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "theme.h"

#include "ui_const.h"

namespace view {

ThemePalette palette(bool dark_mode) {
    const auto& hex = dark_mode ? color::kDark : color::kLight;
    ThemePalette colors{};
    colors.background = lv_color_hex(hex.page);
    colors.surface = lv_color_hex(hex.page);
    colors.bar = lv_color_hex(hex.page);
    colors.tile = lv_color_hex(hex.tile);
    colors.button = lv_color_hex(hex.button);
    colors.border = lv_color_hex(hex.border);
    colors.text = lv_color_hex(hex.text);
    colors.text_secondary = lv_color_hex(hex.text_secondary);
    colors.text_muted = lv_color_hex(hex.text_muted);
    colors.text_disabled = lv_color_hex(hex.text_disabled);
    colors.primary = lv_color_hex(hex.primary);
    colors.info = lv_color_hex(hex.info);
    colors.success = lv_color_hex(hex.success);
    colors.warning = lv_color_hex(hex.warning);
    colors.error = lv_color_hex(hex.error);
    return colors;
}

void apply_lvgl_theme(lv_display_t* display, bool dark_mode, const lv_font_t* font) {
    if (!display) {
        return;
    }

    const auto colors = palette(dark_mode);
    lv_theme_t* theme = lv_theme_default_init(display, colors.primary, colors.info, dark_mode, font);
    lv_display_set_theme(display, theme);
}

} // namespace view
