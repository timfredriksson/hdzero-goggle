#include "ui_update.h"

#include <unistd.h>

#include <lvgl/lvgl.h>

#include "core/update.h"
#include "driver/fans.h"
#include "driver/gpio.h"

LV_IMG_DECLARE(img_logo);

static lv_obj_t *overlay;
static lv_obj_t *label;
static lv_obj_t *progress_bar;

void ui_update_init() {
    overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 1920, 1080);
    lv_obj_set_style_bg_color(overlay, lv_color_make(19, 19, 19), 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_border_side(overlay, LV_BORDER_SIDE_NONE, 0);
    lv_obj_move_foreground(overlay);

    lv_obj_t *logo = lv_img_create(overlay);
    lv_img_set_src(logo, &img_logo);
    lv_obj_set_size(logo, 264, 96);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, 0);

    progress_bar = lv_bar_create(overlay);
    lv_obj_set_size(progress_bar, 680, 20);
    lv_obj_align(progress_bar, LV_ALIGN_CENTER, 0, 104);
    lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);

    label = lv_label_create(overlay);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_recolor(label, true);
    lv_label_set_text(label, "FINALIZING UPDATE... DO NOT POWER OFF...");
    lv_obj_set_width(label, 680);
    lv_obj_set_style_text_color(label, lv_color_make(255, 255, 255), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 144);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
}

static void ui_update_progress(uint32_t val) {
    lv_bar_set_value(progress_bar, val, LV_ANIM_OFF);
    lv_timer_handler();
}

void ui_update_run() {
    if (!update_needs_finalizing()) {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_timer_handler();
        return;
    }

    lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_timer_handler();

    // go full blast on cooling
    fans_right_setspeed(9);
    fans_left_setspeed(9);

    const update_result_t ret = update_finalize(ui_update_progress);

    switch (ret) {
    case UPDATE_SUCCESS:
        lv_label_set_text(label, "#00FF00 Update success, repower goggle NOW!#");
        lv_timer_handler();

        beep();
        usleep(1000000);
        beep();
        usleep(1000000);
        beep();

        break;

    case UPDATE_VERIFY_FAIL:
        lv_label_set_text(label, "#FF0000 Verification failed, try it again#");
        lv_timer_handler();
        break;

    case UPDATE_NOT_FOUND:
        lv_label_set_text(label, "#FFFF00 No firmware found.#");
        lv_timer_handler();
        break;

    case UPDATE_MULTIPLE_FILES:
        lv_label_set_text(label, "#FFFF00 Multiple versions been found. Keep only one.#");
        lv_timer_handler();
        break;

    case UPDATE_ERROR:
        lv_label_set_text(label, "#FF0000 FAILED#");
        lv_timer_handler();
        break;
    }

    while (1) {
        lv_timer_handler();
        usleep(5000);
    }
}