/*
 * Экран центральной половинки.
 *
 * Три квадрата 68x68 сверху вниз (см. canvas.h про поворот):
 *
 *   ┌────────────┐
 *   │ [▓▓▓  ] ⚡ ᛒ│  заряд полоской, зарядка, endpoint
 *   │     87     │  тот же заряд числом, Montserrat 42 (как на периферии)
 *   ├────────────┤
 *   │            │
 *   │    R U     │  язык, Montserrat 42; RU — инверсией
 *   │            │
 *   ├────────────┤
 *   │    nav     │  имя активного слоя (видно только 24 строки)
 *   └────────────┘
 *
 * Кружки BLE-профилей из виджета шилда убраны — их место занял язык. Номер
 * профиля видно по символу endpoint'а: он же показывает, спарен ли профиль и
 * подключён ли.
 *
 * График WPM с этого места убран ради батареи: zmk/app/src/wpm.c будит CPU
 * раз в секунду вечно, а при печати значение меняется почти каждую секунду и
 * с LV_Z_FULL_REFRESH=y уносит в панель полный кадр (~50 мс на 250 кГц) —
 * и всё это только на центральной, отчего половинки разряжались неравномерно.
 * Освободившееся окно занял заряд числом: событие то же самое (батарея, раз в
 * 60 с), новых пробуждений не добавляет.
 *
 * Язык берётся из состояния слоя, а не из HID-индикатора хоста напрямую.
 * Индикатор — источник для ru-layer-sync, а экран показывает результат: то,
 * что клавиатура сейчас печатает. При ручном &tog RU (Windows/macOS, где
 * grp_led:* нет) экран остаётся верным, хотя индикатора нет вовсе.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lang_screen, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>

#include "screen.h"

/* Экран ровно один, список виджетов ZMK тут ни к чему. */
static struct {
    bool ready;
    lv_obj_t *obj;
    uint8_t cbuf_status[CANVAS_BUF_SIZE];
    uint8_t cbuf_lang[CANVAS_BUF_SIZE];
    uint8_t cbuf_layer[CANVAS_BUF_SIZE];

    uint8_t battery;
    bool charging;
    struct zmk_endpoint_instance endpoint;
    bool profile_bonded;
    bool profile_connected;
    bool alt_lang;
    const char *layer_label;
} screen;

/* --- отрисовка ---------------------------------------------------------- */

static void draw_status(void) {
    lv_obj_t *canvas = lv_obj_get_child(screen.obj, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, SCREEN_FG, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_label_dsc_t label_level_dsc;
    init_label_dsc(&label_level_dsc, SCREEN_FG, &lv_font_montserrat_42, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(canvas, SCREEN_BG, LV_OPA_COVER);

    draw_battery(canvas, screen.battery, screen.charging);

    /* Endpoint прижат вправо: полоска заряда занимает 0..32, молния 34..48. */
    const char *endpoint_text = "";
    switch (screen.endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        endpoint_text = LV_SYMBOL_USB;
        break;
    case ZMK_TRANSPORT_BLE:
        if (!screen.profile_bonded) {
            endpoint_text = LV_SYMBOL_SETTINGS;
        } else if (screen.profile_connected) {
            endpoint_text = LV_SYMBOL_WIFI;
        } else {
            endpoint_text = LV_SYMBOL_CLOSE;
        }
        break;
    }
    canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, endpoint_text);

    /* Заряд числом под полоской: 42 строки от 21 до 63 — ровно высота
     * Montserrat 42. Позиция та же, что у числа на периферии, чтобы половинки
     * читались одинаково. */
    char level_text[5] = {};
    snprintf(level_text, sizeof(level_text), "%d", screen.battery);
    canvas_draw_text(canvas, 0, 21, CANVAS_SIZE, &label_level_dsc, level_text);

    rotate_canvas(canvas);
}

static void draw_lang(void) {
    lv_obj_t *canvas = lv_obj_get_child(screen.obj, 1);

    /* Альтернативный язык — инверсией: белым по чёрному его видно, не
     * вчитываясь в буквы, а это единственное, что нужно от индикатора. */
    const lv_color_t bg = screen.alt_lang ? SCREEN_FG : SCREEN_BG;
    const lv_color_t fg = screen.alt_lang ? SCREEN_BG : SCREEN_FG;
    const char *text =
        screen.alt_lang ? CONFIG_ZMK_LANG_SCREEN_LABEL_ALT : CONFIG_ZMK_LANG_SCREEN_LABEL_BASE;

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, fg, &lv_font_montserrat_42, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(canvas, bg, LV_OPA_COVER);
    canvas_draw_text(canvas, 0, 6, CANVAS_SIZE, &label_dsc, text);

    rotate_canvas(canvas);
}

static void draw_layer(void) {
    lv_obj_t *canvas = lv_obj_get_child(screen.obj, 2);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, SCREEN_FG, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(canvas, SCREEN_BG, LV_OPA_COVER);
    canvas_draw_text(canvas, 0, 4, CANVAS_SIZE, &label_dsc,
                     screen.layer_label ? screen.layer_label : "");

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

/* --- endpoint ----------------------------------------------------------- */

struct endpoint_state {
    struct zmk_endpoint_instance selected;
    bool bonded;
    bool connected;
};

static void endpoint_update_cb(struct endpoint_state state) {
    if (!screen.ready) {
        return;
    }
    screen.endpoint = state.selected;
    screen.profile_bonded = state.bonded;
    screen.profile_connected = state.connected;
    draw_status();
}

static struct endpoint_state endpoint_get_state(const zmk_event_t *eh) {
    return (struct endpoint_state){
        .selected = zmk_endpoint_get_selected(),
        .bonded = !zmk_ble_active_profile_is_open(),
        .connected = zmk_ble_active_profile_is_connected(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(lang_screen_endpoint, struct endpoint_state, endpoint_update_cb,
                            endpoint_get_state)
ZMK_SUBSCRIPTION(lang_screen_endpoint, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(lang_screen_endpoint, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(lang_screen_endpoint, zmk_ble_active_profile_changed);
#endif

/* --- слои и язык -------------------------------------------------------- */

struct layer_state {
    bool alt_lang;
    const char *label;
};

static void layer_update_cb(struct layer_state state) {
    if (!screen.ready) {
        return;
    }
    const bool lang_changed = screen.alt_lang != state.alt_lang;

    screen.alt_lang = state.alt_lang;
    screen.layer_label = state.label;

    /* Квадрат языка перерисовывается только когда язык действительно
     * сменился: кадр уходит в панель ~58 мс на 250 кГц, а слой дёргается
     * на каждое удержание большого пальца. */
    if (lang_changed) {
        draw_lang();
    }
    draw_layer();
}

static struct layer_state layer_get_state(const zmk_event_t *eh) {
    const zmk_keymap_layer_id_t lang_id =
        zmk_keymap_layer_index_to_id(CONFIG_ZMK_LANG_SCREEN_LAYER);
    const zmk_keymap_layer_index_t top = zmk_keymap_highest_layer_active();

    /* Имя слоя нужно только для того, что держат руками. База и слой языка
     * и так видны по большому индикатору, дублировать их незачем. */
    const char *label = NULL;
    if (top != 0 && top != CONFIG_ZMK_LANG_SCREEN_LAYER) {
        label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(top));
    }

    return (struct layer_state){
        .alt_lang = (lang_id != ZMK_KEYMAP_LAYER_ID_INVAL) && zmk_keymap_layer_active(lang_id),
        .label = label,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(lang_screen_layer, struct layer_state, layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(lang_screen_layer, zmk_layer_state_changed);

/* --- сборка ------------------------------------------------------------- */

int lang_screen_widget_init(lv_obj_t *parent) {
    /* Кадр LVGL — 160x68; каждый квадрат кладётся так, чтобы после поворота
     * встать в свою треть портретного экрана. Нижнему достаётся 24 строки из
     * 68 — остальное уезжает за левый край кадра, поэтому имя слоя рисуется
     * у самого верха своего квадрата. Смещения те же, что у виджета шилда. */
    screen.obj = lv_obj_create(parent);
    lv_obj_set_size(screen.obj, 160, 68);

    lv_obj_t *status = lv_canvas_create(screen.obj);
    lv_obj_align(status, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(status, screen.cbuf_status, CANVAS_SIZE, CANVAS_SIZE,
                         CANVAS_COLOR_FORMAT);

    lv_obj_t *lang = lv_canvas_create(screen.obj);
    lv_obj_align(lang, LV_ALIGN_TOP_LEFT, 24, 0);
    lv_canvas_set_buffer(lang, screen.cbuf_lang, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    lv_obj_t *layer = lv_canvas_create(screen.obj);
    lv_obj_align(layer, LV_ALIGN_TOP_LEFT, -44, 0);
    lv_canvas_set_buffer(layer, screen.cbuf_layer, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    screen.ready = true;

    /* Квадрат языка рисуется здесь: обработчик слоя трогает его только на
     * смене языка, а с нулевого состояния смены не будет. */
    draw_lang();

    lang_screen_battery_init();
    lang_screen_endpoint_init();
    lang_screen_layer_init();

    lv_obj_align(screen.obj, LV_ALIGN_TOP_LEFT, 0, 0);

    return 0;
}
