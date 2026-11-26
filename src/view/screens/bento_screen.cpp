/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#include "bento_screen.h"

#include "bindings.h"

namespace screen {

BentoScreen::BentoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {}

void BentoScreen::attach_tiles(lv_obj_t* content,
                               const bento::Fonts& fonts,
                               std::array<bento::Tile*, bento::kTileCount> tiles) {
    tiles_ = tiles;
    panel_.build(content, fonts);
    reactive::observe_obj(content, view_model().focused_tile_subject(), changed_cb, this);
    reactive::observe_obj(content, view_model().detail_open_subject(), changed_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), changed_cb, this);
}

void BentoScreen::refresh() {
    refresh_tiles();

    const auto colors = view::palette(view_model().is_dark_mode());
    const int focused = view_model().focused_tile();
    for (size_t i = 0; i < tiles_.size(); ++i) {
        if (tiles_[i]) {
            tiles_[i]->set_focused(static_cast<int>(i) == focused, colors);
        }
    }

    // The panel is rebuilt on every refresh, so its values stay live.
    if (view_model().is_detail_open() && focused >= 0 && static_cast<size_t>(focused) < tiles_.size()) {
        panel_.show(detail(static_cast<size_t>(focused)), colors);
    }
    else {
        panel_.hide();
    }
}

void BentoScreen::changed_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* screen = static_cast<BentoScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen
