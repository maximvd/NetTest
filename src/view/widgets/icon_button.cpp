/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "icon_button.h"

#include "theme.h"

namespace view::widgets {

IconButton::IconButton(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, int32_t width, int32_t height)
    : BaseWidgets(parent), view_model_(view_model), width_(width), height_(height) {}

void IconButton::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_button_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, width_, height_);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    image_ = lv_image_create(core_obj_);
    lv_obj_center(image_);
    lv_obj_set_style_image_recolor_opa(image_, LV_OPA_COVER, 0);

    lv_subject_add_observer_obj(view_model_.dark_mode_subject(), theme_observer_cb, core_obj_, this);
}

void IconButton::set_icon(const std::string& source) {
    if (source == source_ || !image_) {
        return;
    }
    source_ = source;
    if (source_.empty()) {
        lv_obj_add_flag(image_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_image_set_src(image_, source_.c_str());
    lv_obj_remove_flag(image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(image_);
}

void IconButton::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (core_obj_) {
        if (enabled_) {
            lv_obj_add_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
        }
        else {
            lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    apply_style();
}

void IconButton::set_active(bool active) {
    active_ = active;
    apply_style();
}

void IconButton::theme_observer_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* button = static_cast<IconButton*>(lv_observer_get_user_data(observer));
    if (button) {
        button->apply_style();
    }
}

void IconButton::apply_style() {
    if (!image_) {
        return;
    }
    const auto colors = view::palette(view_model_.is_dark_mode());
    const auto color = !enabled_ ? colors.text_disabled : active_ ? colors.primary : colors.text_secondary;
    lv_obj_set_style_image_recolor(image_, color, 0);
}

} // namespace view::widgets
