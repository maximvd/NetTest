/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bento_screen.h"
#include "net_probe.h"

#include <array>

namespace screen {

// Connectivity checks as bento tiles: a wide verdict tile, then one tile per
// check (DHCP servers, gateway ping, 1.1.1.1, DNS) with its figure and detail.
// The TCP 443 check has no tile: it backs up the 1.1.1.1 tile when ping is
// blocked and decides whether the verdict says "Online". Each tile opens into
// a panel with the full results.
class TestsScreen : public BentoScreen {
public:
    TestsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    using Tester = platform::net::ConnectivityTester;

    // Focus order after the verdict tile.
    static constexpr std::array<Tester::Step, 4> kTileSteps = {
        Tester::Dhcp, Tester::GatewayPing, Tester::InternetPing, Tester::Dns};

    void build_content(lv_obj_t* content) override;
    void refresh_tiles() override;
    bento::Detail detail(size_t tile) override;

    bento::Detail verdict_detail(const Tester::Report& report);
    bento::Detail dhcp_detail(const Tester::Report& report);
    bento::Detail step_detail(const Tester::Report& report, Tester::Step step);

    bento::Tile verdict_;
    std::array<bento::Tile, kTileSteps.size()> steps_{};
};

} // namespace screen
