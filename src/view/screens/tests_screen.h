/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "bento.h"
#include "net_probe.h"

#include <array>

namespace screen {

// Connectivity checks as bento tiles: a wide verdict tile, then one tile per
// check (DHCP servers, gateway ping, 1.1.1.1, DNS) with its figure and detail.
// The TCP 443 check has no tile: it backs up the 1.1.1.1 tile when ping is
// blocked and decides whether the verdict says "Online".
class TestsScreen : public BaseScreen {
public:
    TestsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    using Tester = platform::net::ConnectivityTester;

    struct StepTile {
        Tester::Step step;
        bento::Tile tile;
    };

    void build_content(lv_obj_t* content) override;
    void refresh();
    static void tests_changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    bento::Tile verdict_;
    std::array<StepTile, 4> steps_{{
        {Tester::Dhcp, {}},
        {Tester::GatewayPing, {}},
        {Tester::InternetPing, {}},
        {Tester::Dns, {}},
    }};
};

} // namespace screen
