/*
 * Рисование в квадрат 68x68 с поворотом под то, как панель припаяна.
 *
 * Кадр LVGL — 160x68 (ландшафт), а читаем мы экран портретом 68x160, поэтому
 * каждый квадрат рисуется отдельно и поворачивается на 270 градусов перед
 * выводом. После поворота внутренние координаты совпадают с тем, как квадрат
 * виден глазом: x — поперёк экрана, y=0 — верх этого квадрата. То есть рисовать
 * можно «как смотришь», про поворот думать не надо.
 *
 * Помощники и раскладка позаимствованы у виджета шилда nice_view
 * (zmk/app/boards/shields/nice_view/widgets/util.c), он тоже MIT. Свой файл
 * нужен потому, что util.c компилируется только вместе с самим виджетом, а мы
 * его как раз выключаем.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>

#define CANVAS_SIZE 68
/* Самый узкий формат, который умеет поворачивать lv_draw_sw_rotate. */
#define CANVAS_COLOR_FORMAT LV_COLOR_FORMAT_L8
#define CANVAS_BUF_SIZE                                                                            \
    LV_CANVAS_BUF_SIZE(CANVAS_SIZE, CANVAS_SIZE, LV_COLOR_FORMAT_GET_BPP(CANVAS_COLOR_FORMAT),     \
                       LV_DRAW_BUF_STRIDE_ALIGN)

/* Полярность пикселя у этого клона прямая: фон белый, чернила чёрные.
 * Инверсию не вводить — с ней экран пустой, см. CLAUDE.md. */
#define SCREEN_BG lv_color_white()
#define SCREEN_FG lv_color_black()

void rotate_canvas(lv_obj_t *canvas);

void init_label_dsc(lv_draw_label_dsc_t *label_dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align);
void init_rect_dsc(lv_draw_rect_dsc_t *rect_dsc, lv_color_t bg_color);

/* Линии в копии помощников были только под график WPM; вместе с ним ушли и
 * init_line_dsc()/canvas_draw_line(). */
void canvas_draw_rect(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                      lv_draw_rect_dsc_t *draw_dsc);
void canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                      lv_draw_label_dsc_t *draw_dsc, const char *txt);

/* Полоска заряда в левом верхнем углу квадрата: 33x12, при зарядке поверх
 * рисуется символ молнии. */
void draw_battery(lv_obj_t *canvas, uint8_t level, bool charging);
