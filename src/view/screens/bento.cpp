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
constexpr int32_t kRowHeight = (kContentHeight - 2 * kPadY - (kRows - 1) * kGap) / kRows;
constexpr int32_t kRadius = 6;

// Inside a tile. The y offsets are tuned for Noto Sans CJK, whose line boxes are
// tall: they put the label, value and detail baselines at about 14, 33 and 51 px.
constexpr int32_t kInsetX = 6;
constexpr int32_t kLabelY = 2;
constexpr int32_t kValueLargeY = 10;
constexpr int32_t kValueMediumY = 14;
constexpr int32_t kDetailBottomInset = 21;

const lv_font_t* or_fallback(const lv_font_t* font, const lv_font_t* fallback) {
    return font ? font : fallback;
}

} // namespace

Fonts Fonts::load(app::AssetManager& assets) {
    Fonts fonts;
    fonts.label = or_fallback(assets.load_standard_font(10), &lv_font_montserrat_10);
    fonts.value_large = or_fallback(assets.load_standard_font(20), &lv_font_montserrat_20);
    fonts.value_medium = or_fallback(assets.load_standard_font(15), &lv_font_montserrat_14);
    fonts.detail = or_fallback(assets.load_standard_font(11, app::StandardFontWeight::Regular),
                               &lv_font_montserrat_12);
    return fonts;
}

Tile create_tile(lv_obj_t* content, const Fonts& fonts, Cell cell, ValueSize size) {
    // The last column takes the pixels left over by the integer division.
    const int32_t x = kPadX + cell.column * (kColumnWidth + kGap);
    const bool reaches_edge = cell.column + cell.column_span == kColumns;
    const int32_t width = reaches_edge ? view::kScreenWidth - kPadX - x
                                       : cell.column_span * kColumnWidth + (cell.column_span - 1) * kGap;
    const int32_t y = kPadY + cell.row * (kRowHeight + kGap);

    Tile tile;
    tile.width = width;
    tile.box = lv_obj_create(content);
    lv_obj_remove_style_all(tile.box);
    lv_obj_set_pos(tile.box, x, y);
    lv_obj_set_size(tile.box, width, kRowHeight);
    lv_obj_set_style_radius(tile.box, kRadius, 0);
    lv_obj_set_style_bg_opa(tile.box, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(tile.box, true, 0);
    lv_obj_clear_flag(tile.box, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t inner_width = width - 2 * kInsetX;
    tile.label = util::create_label(tile.box, fonts.label, kInsetX, kLabelY, inner_width);
    const bool large = size == ValueSize::Large;
    tile.value = util::create_label(tile.box,
                                    large ? fonts.value_large : fonts.value_medium,
                                    kInsetX,
                                    large ? kValueLargeY : kValueMediumY,
                                    inner_width);
    tile.detail = util::create_label(tile.box,
                                     fonts.detail,
                                     kInsetX,
                                     kRowHeight - kDetailBottomInset,
                                     inner_width,
                                     LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    return tile;
}

void Tile::reserve_right(int32_t reserved) const {
    const int32_t inner_width = width - 2 * kInsetX - reserved;
    lv_obj_set_width(value, inner_width);
    lv_obj_set_width(detail, inner_width);
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

lv_obj_t* create_badge(const Tile& tile, const Fonts& fonts) {
    auto* badge = lv_label_create(tile.box);
    lv_label_set_text(badge, "");
    lv_obj_set_style_text_font(badge, fonts.label, 0);
    lv_obj_set_style_radius(badge, 3, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(badge, 4, 0);
    lv_obj_set_style_pad_ver(badge, 0, 0);
    lv_obj_align(badge, LV_ALIGN_BOTTOM_RIGHT, -kInsetX, -6);
    return badge;
}

void set_badge(lv_obj_t* badge, const std::string& text, lv_color_t background, lv_color_t text_color) {
    util::set_visible(badge, !text.empty());
    util::set_text(badge, text);
    lv_obj_set_style_bg_color(badge, background, 0);
    lv_obj_set_style_text_color(badge, text_color, 0);
}

} // namespace screen::bento
