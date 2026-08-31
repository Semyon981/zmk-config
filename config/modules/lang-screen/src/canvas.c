/*
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include "canvas.h"

void rotate_canvas(lv_obj_t *canvas) {
    uint8_t *buf = lv_canvas_get_draw_buf(canvas)->data;
    static uint8_t buf_copy[CANVAS_BUF_SIZE];
    memcpy(buf_copy, buf, sizeof(buf_copy));

    const uint32_t stride = lv_draw_buf_width_to_stride(CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_draw_sw_rotate(buf_copy, buf, CANVAS_SIZE, CANVAS_SIZE, stride, stride,
                      LV_DISPLAY_ROTATION_270, CANVAS_COLOR_FORMAT);
}

void init_label_dsc(lv_draw_label_dsc_t *label_dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align) {
    lv_draw_label_dsc_init(label_dsc);
    label_dsc->color = color;
    label_dsc->font = font;
    label_dsc->align = align;
}

void init_rect_dsc(lv_draw_rect_dsc_t *rect_dsc, lv_color_t bg_color) {
    lv_draw_rect_dsc_init(rect_dsc);
    rect_dsc->bg_color = bg_color;
}

void canvas_draw_rect(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                      lv_draw_rect_dsc_t *draw_dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_area_t coords = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(&layer, draw_dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}

void canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                      lv_draw_label_dsc_t *draw_dsc, const char *txt) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_dsc->text = txt;
    lv_area_t coords = {x, y, x + max_w, y + CANVAS_SIZE};
    lv_draw_label(&layer, draw_dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}

void draw_battery(lv_obj_t *canvas, uint8_t level, bool charging) {
    lv_draw_rect_dsc_t rect_bg_dsc;
    init_rect_dsc(&rect_bg_dsc, SCREEN_BG);
    lv_draw_rect_dsc_t rect_fg_dsc;
    init_rect_dsc(&rect_fg_dsc, SCREEN_FG);

    /* Корпус, внутренность, заливка по уровню, «пипка» справа. */
    canvas_draw_rect(canvas, 0, 2, 29, 12, &rect_fg_dsc);
    canvas_draw_rect(canvas, 1, 3, 27, 10, &rect_bg_dsc);
    canvas_draw_rect(canvas, 2, 4, (level + 2) / 4, 8, &rect_fg_dsc);
    canvas_draw_rect(canvas, 30, 5, 3, 6, &rect_fg_dsc);
    canvas_draw_rect(canvas, 31, 6, 1, 4, &rect_bg_dsc);

    if (charging) {
        /* Символ из шрифта, а не картинка: так модулю не нужен ни LV_USE_IMAGE,
         * ни собственная копия bolt.c. */
        lv_draw_label_dsc_t bolt_dsc;
        init_label_dsc(&bolt_dsc, SCREEN_FG, &lv_font_montserrat_16, LV_TEXT_ALIGN_LEFT);
        canvas_draw_text(canvas, 34, 0, 20, &bolt_dsc, LV_SYMBOL_CHARGE);
    }
}
