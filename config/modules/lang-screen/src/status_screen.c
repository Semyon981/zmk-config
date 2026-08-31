/*
 * Точка входа экрана.
 *
 * zmk_display_status_screen() в ZMK объявлен weak (app/src/display/main.c),
 * сильное определение забирает тот, кто его дал. Обычно это виджет шилда
 * nice_view; чтобы встать на его место, раскладка ставит
 * CONFIG_NICE_VIEW_WIDGET_STATUS=n — иначе два сильных определения и ошибка
 * линковки.
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include "screen.h"

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lang_screen_widget_init(screen);

    return screen;
}
