/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"
#include "network_viewmodel.h"

#include "lvgl.h"

#include <string>

namespace viewmodel {

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* quit_requested_subject();
    // Tile with the focus ring (0 = top-left wide tile, 1 = top-right,
    // 2..4 = bottom row left to right), or -1 when none is focused.
    lv_subject_t* focused_tile_subject();
    lv_subject_t* detail_open_subject(); // the focused tile's detail panel is open

    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    bool is_link_page() const; // Ethernet or Wi-Fi
    LinkKind current_link_kind() const; // Wireless on the Wi-Fi page, else Wired
    void show_ethernet_page();
    void show_wifi_page();
    void show_tests_page();
    void show_last_link_page();
    void request_quit();

    // Arrow keys: the first press shows the ring on the top-left tile, later
    // presses move it across the 3 x 2 grid.
    enum class Direction { Left, Right, Up, Down };
    void move_focus(Direction direction);
    int focused_tile() const;
    bool is_detail_open() const;
    void open_detail();   // opens the focused tile (focusing the first one if none is)
    // ESC: closes an open panel, else hides the ring. Returns false when there was nothing to close.
    bool close_detail_or_focus();

    NetworkViewModel& network();

private:
    void publish_all();
    // Title bar text: the selected interface and its position on link pages.
    std::string page_title() const;
    static void link_changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    model::BaseModel model_;
    reactive::StringSubject<32> title_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::BoolSubject quit_requested_subject_;
    reactive::IntSubject focused_tile_subject_{-1};
    reactive::BoolSubject detail_open_subject_{false};
    NetworkViewModel network_;
    lv_observer_t* link_observer_{nullptr};
};

} // namespace viewmodel
