/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */



#pragma once

#include <cstdint>

namespace view {

// Phosphor-Fill.ttf glyphs (see assets/fonts/LICENSE.txt)
constexpr const char* ICON_MOON               = "\uE330";
constexpr const char* ICON_SUN                = "\uE474";
constexpr const char* ICON_HEARTBEAT          = "\uE2AC";
constexpr const char* ICON_PLAY               = "\uE3D0";
constexpr const char* ICON_ARROWS_DOWN_UP     = "\uE098";
constexpr const char* ICON_WIFI_NONE          = "\uE4F0";
constexpr const char* ICON_WIFI_LOW           = "\uE4EC";
constexpr const char* ICON_WIFI_MEDIUM        = "\uE4EE";
constexpr const char* ICON_WIFI_HIGH          = "\uE4EA";
constexpr const char* ICON_ETHERNET           = "\uEDDE";
constexpr const char* ICON_BAT_FULL           = "\uE7C4";
constexpr const char* ICON_BAT_HIGH           = "\uE7C2";
constexpr const char* ICON_BAT_MEDIUM         = "\uE7C0";
constexpr const char* ICON_BAT_LOW            = "\uE7BE";
constexpr const char* ICON_BAT_EMPTY          = "\uE7C6";
constexpr const char* ICON_BAT_CHARGING       = "\uE0BC";

namespace color {

struct PaletteHex {
    uint32_t page;           // page, content area and both bars
    uint32_t tile;           // bento tiles
    uint32_t button;
    uint32_t border;
    uint32_t text;           // values
    uint32_t text_secondary; // detail lines under a value
    uint32_t text_muted;     // tile labels
    uint32_t text_disabled;  // unavailable values and disabled icons
    uint32_t primary;        // accent: active page, healthy link
    uint32_t info;
    uint32_t success;
    uint32_t warning;
    uint32_t error;
};

// "Bento" palette: a flat page with rounded tiles, plain text and one accent
// colour per state. Text colours keep 4.5:1 contrast on their tiles.
constexpr PaletteHex kDark{
    0x0e1114, 0x1a1f25, 0x1a1f25, 0x2a313a, 0xeef1f4, 0xc3cad1, 0x9aa4ae,
    0x5b6570, 0x4ade80, 0x60a5fa, 0x4ade80, 0xfbbf24, 0xf87171,
};

constexpr PaletteHex kLight{
    0xe7eaee, 0xffffff, 0xffffff, 0xcdd2d8, 0x111827, 0x374151, 0x4b5563,
    0x9ca3af, 0x15803d, 0x1d4ed8, 0x15803d, 0xb45309, 0xb42318,
};

} // namespace color

} // namespace view
