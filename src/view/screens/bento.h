/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"
#include "theme.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace app {
class AssetManager;
}

// A 3 x 2 grid of rounded tiles filling a screen's content area (the top row
// a little taller), plus a detail panel that covers the grid when a tile is
// opened. Each tile has a small upper-case label, one large value and a
// detail line; values too long for their tile scroll back and forth.
namespace screen::bento {

constexpr int32_t kColumns = 3;
constexpr int32_t kRows = 2;
constexpr size_t kTileCount = 5; // the wide top-left tile, top-right, then the bottom row

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
    const lv_font_t* name{nullptr};         // a second, emphasised line (switch name)
    const lv_font_t* panel_value{nullptr};
    const lv_font_t* panel_suffix{nullptr};

    static Fonts load(app::AssetManager& assets);
};

// What a value means, which sets its colour.
enum class Tone { Normal, Accent, Warning, Error, Muted };

lv_color_t tone_color(Tone tone, const view::ThemePalette& colors);

struct Tile {
    lv_obj_t* box{nullptr};
    lv_obj_t* label{nullptr};
    lv_obj_t* value{nullptr};
    lv_obj_t* detail{nullptr};
    int32_t width{0}; // kept here: LVGL only reports sizes after the next layout pass
    int32_t height{0};

    // Sets the text and colours; call again after a theme change.
    void set(const std::string& label_text,
             const std::string& value_text,
             Tone tone,
             const std::string& detail_text,
             const view::ThemePalette& colors) const;
    // Narrows the value and detail lines to leave room on the right.
    void reserve_right(int32_t reserved) const;
    void set_focused(bool focused, const view::ThemePalette& colors) const;
};

Tile create_tile(lv_obj_t* content, const Fonts& fonts, Cell cell, ValueSize size);

// What a tile shows when it is opened: a header, one big value with a short
// suffix, and up to eight label/value rows (two per line, or one when wide).
struct DetailRow {
    std::string key;
    std::string value;
    Tone tone{Tone::Normal};
    bool wide{false};
};

struct Detail {
    std::string header;
    std::string value;
    std::string suffix;
    Tone value_tone{Tone::Normal};
    Tone suffix_tone{Tone::Normal};
    std::vector<DetailRow> rows;
};

class DetailPanel {
public:
    static constexpr size_t kMaxRows = 8;

    void build(lv_obj_t* content, const Fonts& fonts);
    void show(const Detail& detail, const view::ThemePalette& colors);
    void hide();

private:
    struct Row {
        lv_obj_t* box{nullptr};
        lv_obj_t* key{nullptr};
        lv_obj_t* value{nullptr};
    };

    lv_obj_t* panel_{nullptr};
    lv_obj_t* header_{nullptr};
    lv_obj_t* close_hint_{nullptr};
    lv_obj_t* value_{nullptr};
    lv_obj_t* suffix_{nullptr};
    lv_obj_t* rows_box_{nullptr};
    std::array<Row, kMaxRows> rows_{};
};

} // namespace screen::bento
