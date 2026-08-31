/*
 * Экран периферийной половинки.
 *
 * Про язык и слои она не знает ничего — keymap живёт на центральной, событий
 * слоя сюда не приходит. Показывать нечего, кроме собственного питания и
 * связи, зато под них есть весь экран: заряд крупным числом видно с другого
 * конца стола, а именно за ним на правую половинку и смотрят.
 *
 *   ┌────────────┐
 *   │ [▓▓▓  ] ⚡ ᛒ│  заряд полоской, зарядка, связь с центральной
 *   ├────────────┤
 *   │            │
 *   │     87     │  тот же заряд числом, Montserrat 42
 *   │            │
 *   ├────────────┤
 *   │            │  пусто
 *   └────────────┘
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "screen.h"

static struct {
    bool ready;
    lv_obj_t *obj;
    uint8_t cbuf_status[CANVAS_BUF_SIZE];
    uint8_t cbuf_level[CANVAS_BUF_SIZE];
    uint8_t cbuf_blank[CANVAS_BUF_SIZE];

    uint8_t battery;
    bool charging;
    bool connected;
} screen;

/* --- отрисовка ---------------------------------------------------------- */

static void draw_status(void) {
    lv_obj_t *canvas = lv_obj_get_child(screen.obj, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, SCREEN_FG, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);

    lv_canvas_fill_bg(canvas, SCREEN_BG, LV_OPA_COVER);
    draw_battery(canvas, screen.battery, screen.charging);
    canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                     screen.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    rotate_canvas(canvas);
}

static void draw_level(void) {
    lv_obj_t *canvas = lv_obj_get_child(screen.obj, 1);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, SCREEN_FG, &lv_font_montserrat_42, LV_TEXT_ALIGN_CENTER);

    char text[5] = {};
    snprintf(text, sizeof(text), "%d", screen.battery);

    lv_canvas_fill_bg(canvas, SCREEN_BG, LV_OPA_COVER);
    canvas_draw_text(canvas, 0, 6, CANVAS_SIZE, &label_dsc, text);

    rotate_canvas(canvas);
}

/* --- заряд -------------------------------------------------------------- */

struct battery_state {
    uint8_t level;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    bool usb_present;
#endif
};

static void battery_update_cb(struct battery_state state) {
    if (!screen.ready) {
        return;
    }
    screen.battery = state.level;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    screen.charging = state.usb_present;
#endif
    draw_status();
    draw_level();
}

static struct battery_state battery_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(lang_screen_battery, struct battery_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(lang_screen_battery, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(lang_screen_battery, zmk_usb_conn_state_changed);
#endif

/* --- связь с центральной ------------------------------------------------ */

struct link_state {
    bool connected;
};

static void link_update_cb(struct link_state state) {
    if (!screen.ready) {
        return;
    }
    screen.connected = state.connected;
    draw_status();
}

static struct link_state link_get_state(const zmk_event_t *eh) {
    return (struct link_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

ZMK_DISPLAY_WIDGET_LISTENER(lang_screen_link, struct link_state, link_update_cb, link_get_state)
ZMK_SUBSCRIPTION(lang_screen_link, zmk_split_peripheral_status_changed);

/* --- сборка ------------------------------------------------------------- */

int lang_screen_widget_init(lv_obj_t *parent) {
    screen.obj = lv_obj_create(parent);
    lv_obj_set_size(screen.obj, 160, 68);

    lv_obj_t *status = lv_canvas_create(screen.obj);
    lv_obj_align(status, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(status, screen.cbuf_status, CANVAS_SIZE, CANVAS_SIZE,
                         CANVAS_COLOR_FORMAT);

    lv_obj_t *level = lv_canvas_create(screen.obj);
    lv_obj_align(level, LV_ALIGN_TOP_LEFT, 24, 0);
    lv_canvas_set_buffer(level, screen.cbuf_level, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    /* Нижняя полоска пустая, но квадрат нужен: иначе 24 столбца кадра никто
     * не пишет и туда попадает мусор из буфера. */
    lv_obj_t *blank = lv_canvas_create(screen.obj);
    lv_obj_align(blank, LV_ALIGN_TOP_LEFT, -44, 0);
    lv_canvas_set_buffer(blank, screen.cbuf_blank, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_canvas_fill_bg(blank, SCREEN_BG, LV_OPA_COVER);

    screen.ready = true;

    lang_screen_battery_init();
    lang_screen_link_init();

    lv_obj_align(screen.obj, LV_ALIGN_TOP_LEFT, 0, 0);

    return 0;
}
