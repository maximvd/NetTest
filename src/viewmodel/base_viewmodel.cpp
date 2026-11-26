/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.app_title()),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      quit_requested_subject_(false) {
    // Open on the page for whichever link carries the internet connection.
    if (network_.default_route_kind() == LinkKind::Wireless) {
        show_wifi_page();
    }
    // Interfaces come and go, and the selection moves: keep the title in step.
    link_observer_ = lv_subject_add_observer(network_.link_revision_subject(), link_changed_cb, this);
}

BaseViewModel::~BaseViewModel() {
    if (link_observer_) {
        lv_observer_remove(link_observer_);
    }
}

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::dark_mode_subject() {
    return dark_mode_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    publish_all();
}

void BaseViewModel::toggle_dark_mode() {
    model_.toggle_dark_mode();
    publish_all();
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

bool BaseViewModel::is_link_page() const {
    return model_.current_page() != model::AppPage::Tests;
}

LinkKind BaseViewModel::current_link_kind() const {
    return model_.current_page() == model::AppPage::Wifi ? LinkKind::Wireless : LinkKind::Wired;
}

void BaseViewModel::show_ethernet_page() {
    model_.set_current_page(model::AppPage::Ethernet);
    publish_all();
}

void BaseViewModel::show_wifi_page() {
    model_.set_current_page(model::AppPage::Wifi);
    publish_all();
}

void BaseViewModel::show_last_link_page() {
    model_.set_current_page(model_.last_link_page());
    publish_all();
}

void BaseViewModel::show_tests_page() {
    model_.set_current_page(model::AppPage::Tests);
    publish_all();
    // First visit runs the checks straight away; later visits keep the last result.
    if (network_.test_report().runs == 0) {
        network_.run_tests();
    }
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

NetworkViewModel& BaseViewModel::network() {
    return network_;
}

std::string BaseViewModel::page_title() const {
    if (!is_link_page()) {
        return "Checks";
    }
    const auto kind = current_link_kind();
    const auto* info = network_.selected_interface(kind);
    if (!info) {
        return kind == LinkKind::Wired ? "No Ethernet" : "No Wi-Fi";
    }
    const auto count = network_.interfaces(kind).size();
    return info->name + "  " + std::to_string(network_.selected_position(kind) + 1) + "/" + std::to_string(count);
}

void BaseViewModel::link_changed_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* view_model = static_cast<BaseViewModel*>(lv_observer_get_user_data(observer));
    if (view_model) {
        view_model->title_subject_.set(view_model->page_title().c_str());
    }
}

void BaseViewModel::publish_all() {
    title_subject_.set(page_title().c_str());
    dark_mode_subject_.set(model_.dark_mode());
    current_page_subject_.set(page_to_int(model_.current_page()));
}

} // namespace viewmodel
