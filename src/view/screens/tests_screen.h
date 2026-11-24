/*
 * SPDX-FileCopyrightText: 2026 Maxim Van Damme
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "net_probe.h"

#include <array>

namespace screen {

// Ping / DNS / internet checks: one row per step with a status dot, the step
// name and its result, plus a one-line summary.
class TestsScreen : public BaseScreen {
public:
    TestsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    struct Row {
        lv_obj_t* dot{nullptr};
        lv_obj_t* name{nullptr};
        lv_obj_t* value{nullptr};
    };

    void build_content(lv_obj_t* content) override;
    void refresh();
    static void tests_changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    std::array<Row, platform::net::ConnectivityTester::StepCount> rows_{};
    lv_obj_t* summary_label_{nullptr};
};

} // namespace screen
