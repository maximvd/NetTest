/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "bento.h"

#include <array>

namespace screen {

// A page of five bento tiles with a focus ring (moved by the arrow keys) and a
// detail panel for the focused tile (Enter opens it, ESC closes it). Subclasses
// create the tiles, fill them in refresh() and describe them in detail().
class BentoScreen : public BaseScreen {
protected:
    BentoScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

    // Call at the end of build_content(), with the tiles in focus order
    // (wide top-left, top-right, bottom left to right).
    void attach_tiles(lv_obj_t* content, const bento::Fonts& fonts, std::array<bento::Tile*, bento::kTileCount> tiles);

    // Refreshes the tiles; the panel and focus ring follow.
    virtual void refresh_tiles() = 0;
    virtual bento::Detail detail(size_t tile) = 0;

    // Observer callback for anything that changes what the tiles show.
    static void changed_cb(lv_observer_t* observer, lv_subject_t* subject);
    void refresh();

private:
    std::array<bento::Tile*, bento::kTileCount> tiles_{};
    bento::DetailPanel panel_;
};

} // namespace screen
