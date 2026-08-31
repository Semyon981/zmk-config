/*
 * Общий интерфейс виджета: реализация одна на роль (src/central.c или
 * src/peripheral.c), выбирает её CMakeLists.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#include "canvas.h"

int lang_screen_widget_init(lv_obj_t *parent);
