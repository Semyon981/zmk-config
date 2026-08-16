/*
 * Слой раскладки следует за HID-индикатором хоста.
 *
 * Хост шлёт клавиатуре состояние Num/Caps/Scroll Lock тем же механизмом,
 * которым зажигается лампочка CapsLock. Если в ОС индикатор привязан к группе
 * раскладки (xkb: grp_led:scroll), клавиатура узнаёт активный язык и сама
 * держит нужный слой — даже когда язык переключили мимо неё.
 *
 * Слой трогается только когда изменился НАШ бит, а не любой бит маски. Это
 * важно для хостов без привязки индикатора к раскладке (Windows, macOS): там
 * язык переключают вручную через &tog, и без этой проверки нажатие Caps Lock
 * или переподключение — оба присылают событие с нулевым битом Scroll — молча
 * сбрасывали бы слой. На Linux поведение не меняется: там бит как раз и ходит
 * вместе с группой.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(ru_layer_sync, CONFIG_ZMK_LOG_LEVEL);

/* -1 — бит ещё не видели: первое событие после старта применяется всегда,
 * чтобы подхватить состояние хоста при подключении. */
static int last_bit = -1;

static int ru_layer_sync_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const bool want = (ev->indicators & CONFIG_ZMK_RU_LAYER_SYNC_INDICATOR) != 0;
    if (last_bit == (int)want) {
        /* Дёрнулся какой-то другой индикатор — не наше дело. */
        return ZMK_EV_EVENT_BUBBLE;
    }
    last_bit = want;

    const zmk_keymap_layer_id_t layer =
        zmk_keymap_layer_index_to_id(CONFIG_ZMK_RU_LAYER_SYNC_LAYER);
    if (layer == ZMK_KEYMAP_LAYER_ID_INVAL) {
        LOG_WRN("layer index %d not found", CONFIG_ZMK_RU_LAYER_SYNC_LAYER);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (want == zmk_keymap_layer_active(layer)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* locking = true, как у &tog: слой должен пережить отпускание клавиш. */
    if (want) {
        zmk_keymap_layer_activate(layer, true);
    } else {
        zmk_keymap_layer_deactivate(layer, true);
    }

    LOG_DBG("indicators 0x%02x -> layer %d %s", ev->indicators, layer, want ? "on" : "off");

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(ru_layer_sync, ru_layer_sync_listener);
ZMK_SUBSCRIPTION(ru_layer_sync, zmk_hid_indicators_changed);
