/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"
#include "theme.h"

#include <cstdint>
#include <string>

namespace app {
class AssetManager;
}

// A 3 x 2 grid of rounded tiles filling a screen's content area. Each tile has
// a small upper-case label, one large value and a detail line under it.
namespace screen::bento {

constexpr int32_t kColumns = 3;
constexpr int32_t kRows = 2;

// A tile's place in the grid; spans let one tile cover two columns.
struct Cell {
    int32_t column;
    int32_t row;
    int32_t column_span{1};
};

enum class ValueSize { Large, Medium };

struct Fonts {
    const lv_font_t* label{nullptr};
    const lv_font_t* value_large{nullptr};
    const lv_font_t* value_medium{nullptr};
    const lv_font_t* detail{nullptr};

    static Fonts load(app::AssetManager& assets);
};

// What a tile's value means, which sets its colour.
enum class Tone { Normal, Accent, Warning, Error, Muted };

struct Tile {
    lv_obj_t* box{nullptr};
    lv_obj_t* label{nullptr};
    lv_obj_t* value{nullptr};
    lv_obj_t* detail{nullptr};
    int32_t width{0}; // kept here: LVGL only reports sizes after the next layout pass

    // Sets the text and colours; call again after a theme change.
    void set(const std::string& label_text,
             const std::string& value_text,
             Tone tone,
             const std::string& detail_text,
             const view::ThemePalette& colors) const;
    // Narrows the value and detail lines to leave room on the right (badges, bars).
    void reserve_right(int32_t reserved) const;
};

Tile create_tile(lv_obj_t* content, const Fonts& fonts, Cell cell, ValueSize size);

lv_color_t tone_color(Tone tone, const view::ThemePalette& colors);

// A small rounded badge (e.g. the VLAN number) pinned to a tile's bottom-right corner.
lv_obj_t* create_badge(const Tile& tile, const Fonts& fonts);
void set_badge(lv_obj_t* badge, const std::string& text, lv_color_t background, lv_color_t text_color);

} // namespace screen::bento
