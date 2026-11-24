/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace model {

enum class AppPage {
    Ethernet = 0, // wired interfaces, addresses, switch/port
    Wifi     = 1, // wireless interfaces, addresses, SSID/signal
    Tests    = 2, // ping / DNS / internet checks
};

class BaseModel {
public:
    const char* app_title() const;

    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);
    // The Ethernet or Wi-Fi page shown most recently (where ESC on Tests returns).
    AppPage last_link_page() const;

private:
    bool dark_mode_ = true;
    AppPage current_page_ = AppPage::Ethernet;
    AppPage last_link_page_ = AppPage::Ethernet;
};

} // namespace model
