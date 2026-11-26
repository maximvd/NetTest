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
    NetworkViewModel network_;
    lv_observer_t* link_observer_{nullptr};
};

} // namespace viewmodel
