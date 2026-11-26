/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "bento.h"

#include "asset_manager.h"
#include "screen_utils.h"

namespace screen::bento {
namespace {

constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kNavBarHeight;
constexpr int32_t kPadX = 4;
constexpr int32_t kPadY = 2;
constexpr int32_t kGap = 4;
constexpr int32_t kColumnWidth = (view::kScreenWidth - 2 * kPadX - (kColumns - 1) * kGap) / kColumns;
// The top row holds the headline tiles (switch port, network, verdict) and gets more room.
constexpr int32_t kBottomRowHeight = 54;
constexpr int32_t kTopRowHeight = kContentHeight - 2 * kPadY - kGap - kBottomRowHeight;
constexpr int32_t kRadius = 6;

// Inside a tile. The offsets are tuned for Noto Sans CJK, whose line boxes are
// tall (1.45 em, baseline at 1.16 em).
constexpr int32_t kInsetX = 6;
constexpr int32_t kLabelY = 2;
constexpr int32_t kValueLargeY = 10;
constexpr int32_t kValueMediumY = 13;
constexpr int32_t kDetailBottomInset = 19;

// The detail panel covers the whole grid.
constexpr int32_t kPanelInsetX = 8;
constexpr int32_t kPanelWidth = view::kScreenWidth - 2 * kPadX;
constexpr int32_t kPanelHeight = kContentHeight - 2 * kPadY;
constexpr int32_t kPanelInnerWidth = kPanelWidth - 2 * kPanelInsetX;
constexpr int32_t kPanelRowHeight = 16;
constexpr int32_t kPanelRowGap = 10;
constexpr int32_t kPanelKeyWidth = 52;

const lv_font_t* or_fallback(const lv_font_t* font, const lv_font_t* fallback) {
    return font ? font : fallback;
}

int32_t row_y(int32_t row) {
    return kPadY + (row == 0 ? 0 : kTopRowHeight + kGap);
}

int32_t row_height(int32_t row) {
    return row == 0 ? kTopRowHeight : kBottomRowHeight;
}

lv_obj_t* create_box(lv_obj_t* parent) {
    auto* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

} // namespace

Fonts Fonts::load(app::AssetManager& assets) {
    using Weight = app::StandardFontWeight;
    Fonts fonts;
    fonts.label = or_fallback(assets.load_standard_font(10), &lv_font_montserrat_10);
    fonts.value_large = or_fallback(assets.load_standard_font(20), &lv_font_montserrat_20);
    fonts.value_medium = or_fallback(assets.load_standard_font(15), &lv_font_montserrat_14);
    fonts.detail = or_fallback(assets.load_standard_font(12, Weight::Regular), &lv_font_montserrat_12);
    fonts.name = or_fallback(assets.load_standard_font(14), &lv_font_montserrat_14);
    fonts.panel_value = or_fallback(assets.load_standard_font(24), &lv_font_montserrat_24);
    fonts.panel_suffix = or_fallback(assets.load_standard_font(13), &lv_font_montserrat_12);
    return fonts;
}

lv_color_t tone_color(Tone tone, const view::ThemePalette& colors) {
    switch (tone) {
        case Tone::Accent:
            return colors.primary;
        case Tone::Warning:
            return colors.warning;
        case Tone::Error:
            return colors.error;
        case Tone::Muted:
            return colors.text_disabled;
        case Tone::Normal:
        default:
            return colors.text;
    }
}

Tile create_tile(lv_obj_t* content, const Fonts& fonts, Cell cell, ValueSize size) {
    // The last column takes the pixels left over by the integer division.
    const int32_t x = kPadX + cell.column * (kColumnWidth + kGap);
    const bool reaches_edge = cell.column + cell.column_span == kColumns;
    const int32_t width = reaches_edge ? view::kScreenWidth - kPadX - x
                                       : cell.column_span * kColumnWidth + (cell.column_span - 1) * kGap;

    Tile tile;
    tile.width = width;
    tile.height = row_height(cell.row);
    tile.box = create_box(content);
    lv_obj_set_pos(tile.box, x, row_y(cell.row));
    lv_obj_set_size(tile.box, width, tile.height);
    lv_obj_set_style_radius(tile.box, kRadius, 0);
    lv_obj_set_style_bg_opa(tile.box, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(tile.box, true, 0);

    const int32_t inner_width = width - 2 * kInsetX;
    const bool large = size == ValueSize::Large;
    tile.label = util::create_label(tile.box, fonts.label, kInsetX, kLabelY, inner_width);
    // Long values (IPv4 with three-digit octets, IPv6, Wi-Fi names) scroll back and forth.
    tile.value = util::create_label(tile.box,
                                    large ? fonts.value_large : fonts.value_medium,
                                    kInsetX,
                                    large ? kValueLargeY : kValueMediumY,
                                    inner_width,
                                    LV_LABEL_LONG_MODE_SCROLL);
    tile.detail = util::create_label(tile.box,
                                     fonts.detail,
                                     kInsetX,
                                     tile.height - kDetailBottomInset,
                                     inner_width,
                                     LV_LABEL_LONG_MODE_SCROLL);
    return tile;
}

void Tile::set(const std::string& label_text,
               const std::string& value_text,
               Tone tone,
               const std::string& detail_text,
               const view::ThemePalette& colors) const {
    lv_obj_set_style_bg_color(box, colors.tile, 0);
    lv_obj_set_style_text_color(label, colors.text_muted, 0);
    lv_obj_set_style_text_color(value, tone_color(tone, colors), 0);
    lv_obj_set_style_text_color(detail, colors.text_secondary, 0);
    util::set_text(label, label_text);
    util::set_text(value, value_text);
    util::set_text(detail, detail_text);
}

void Tile::reserve_right(int32_t reserved) const {
    const int32_t inner_width = width - 2 * kInsetX - reserved;
    lv_obj_set_width(value, inner_width);
    lv_obj_set_width(detail, inner_width);
}

void Tile::set_focused(bool focused, const view::ThemePalette& colors) const {
    // An outline drawn inside the tile, so the text does not move when focused.
    lv_obj_set_style_outline_width(box, focused ? 2 : 0, 0);
    lv_obj_set_style_outline_pad(box, -2, 0);
    lv_obj_set_style_outline_color(box, colors.primary, 0);
}

void DetailPanel::build(lv_obj_t* content, const Fonts& fonts) {
    panel_ = create_box(content);
    lv_obj_set_pos(panel_, kPadX, kPadY);
    lv_obj_set_size(panel_, kPanelWidth, kPanelHeight);
    lv_obj_set_style_radius(panel_, 8, 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_, 1, 0);

    header_ = util::create_label(panel_, fonts.label, kPanelInsetX, 3, kPanelInnerWidth - 30);

    close_hint_ = lv_label_create(panel_);
    lv_label_set_text(close_hint_, "ESC");
    lv_obj_set_style_text_font(close_hint_, fonts.label, 0);
    lv_obj_set_style_border_width(close_hint_, 1, 0);
    lv_obj_set_style_radius(close_hint_, 3, 0);
    lv_obj_set_style_pad_hor(close_hint_, 3, 0);
    lv_obj_align(close_hint_, LV_ALIGN_TOP_RIGHT, -6, 3);

    // The big value and its suffix share a line, bottom-aligned.
    auto* headline = create_box(panel_);
    lv_obj_set_pos(headline, kPanelInsetX, 14);
    lv_obj_set_size(headline, kPanelInnerWidth, 36);
    lv_obj_set_flex_flow(headline, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(headline, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(headline, 8, 0);
    value_ = lv_label_create(headline);
    lv_label_set_text(value_, "");
    lv_obj_set_style_text_font(value_, fonts.panel_value, 0);
    lv_obj_set_style_max_width(value_, kPanelInnerWidth, 0);
    lv_label_set_long_mode(value_, LV_LABEL_LONG_MODE_SCROLL);
    suffix_ = lv_label_create(headline);
    lv_label_set_text(suffix_, "");
    lv_obj_set_style_text_font(suffix_, fonts.panel_suffix, 0);
    lv_obj_set_style_pad_bottom(suffix_, 3, 0);
    lv_obj_set_flex_grow(suffix_, 1);
    lv_label_set_long_mode(suffix_, LV_LABEL_LONG_MODE_DOTS);

    rows_box_ = create_box(panel_);
    lv_obj_set_width(rows_box_, kPanelInnerWidth);
    lv_obj_set_flex_flow(rows_box_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(rows_box_, kPanelRowGap, 0);
    for (auto& row : rows_) {
        row.box = create_box(rows_box_);
        lv_obj_set_height(row.box, kPanelRowHeight);
        row.key = util::create_label(row.box, fonts.label, 0, 2, kPanelKeyWidth, LV_LABEL_LONG_MODE_CLIP);
        row.value = util::create_label(row.box, fonts.detail, kPanelKeyWidth + 2, 0, 0, LV_LABEL_LONG_MODE_SCROLL);
    }

    hide();
}

void DetailPanel::show(const Detail& detail, const view::ThemePalette& colors) {
    lv_obj_set_style_bg_color(panel_, colors.tile, 0);
    lv_obj_set_style_border_color(panel_, colors.border, 0);
    lv_obj_set_style_text_color(header_, colors.text_muted, 0);
    lv_obj_set_style_text_color(close_hint_, colors.text_muted, 0);
    lv_obj_set_style_border_color(close_hint_, colors.border, 0);
    lv_obj_set_style_text_color(value_, tone_color(detail.value_tone, colors), 0);
    lv_obj_set_style_text_color(suffix_,
                                detail.suffix_tone == Tone::Normal ? colors.text_secondary
                                                                   : tone_color(detail.suffix_tone, colors),
                                0);
    util::set_text(header_, detail.header);
    util::set_text(value_, detail.value);
    util::set_text(suffix_, detail.suffix);

    // Rows fill two columns; a wide row takes a whole line.
    const int32_t half_width = (kPanelInnerWidth - kPanelRowGap) / 2;
    int32_t slots = 0;
    for (size_t i = 0; i < rows_.size(); ++i) {
        auto& row = rows_[i];
        const bool used = i < detail.rows.size();
        util::set_visible(row.box, used);
        if (!used) {
            continue;
        }
        const auto& data = detail.rows[i];
        const int32_t width = data.wide ? kPanelInnerWidth : half_width;
        if (data.wide && slots % 2 == 1) {
            ++slots; // the wide row starts a new line
        }
        slots += data.wide ? 2 : 1;
        lv_obj_set_width(row.box, width);
        lv_obj_set_width(row.value, width - kPanelKeyWidth - 2);
        lv_obj_set_style_text_color(row.key, colors.text_muted, 0);
        lv_obj_set_style_text_color(row.value,
                                    data.tone == Tone::Normal ? colors.text : tone_color(data.tone, colors),
                                    0);
        util::set_text(row.key, data.key);
        util::set_text(row.value, data.value);
    }
    const int32_t lines = (slots + 1) / 2;
    lv_obj_set_height(rows_box_, lines * kPanelRowHeight);
    lv_obj_set_pos(rows_box_, kPanelInsetX, kPanelHeight - 2 - 4 - lines * kPanelRowHeight);

    util::set_visible(panel_, true);
    lv_obj_move_foreground(panel_);
}

void DetailPanel::hide() {
    if (panel_) {
        util::set_visible(panel_, false);
    }
}

} // namespace screen::bento
