/*
 * Сторож бутлоадера: позволяет перепрошить плату без двойного ресета.
 *
 * Два способа, оба с хоста:
 *   printf 'dfu!' > /dev/ttyACM0       — команда в консоль;
 *   stty -F /dev/ttyACM0 1200          — «касание 1200 бод», как на Arduino.
 *
 * Команда именно четырёхбайтовая: одиночный байт ловил ложные срабатывания —
 * ModemManager зондирует свежий /dev/ttyACM* AT-командами, и плата уходила в
 * бутлоадер сама, ещё до первой строки лога.
 *
 * Магия 0x57 кладётся в GPREGRET вручную: бутлоадер Adafruit по этому
 * значению остаётся в UF2-режиме и поднимает том NICENANO. То же значение
 * использует ZMK (`zmk/dt-bindings/zmk/reset.h`), и тот же регистр —
 * `gpregret1` (0x4000051C) из `nrf52840_uf2_boot_mode.dtsi`.
 *
 * Аргумент sys_reboot() тут не годится: `sys_arch_reboot()` на Cortex-M
 * (`zephyr/arch/arm/core/cortex_m/scb.c:38`) делает ARG_UNUSED(type) и просто
 * дёргает NVIC_SystemReset — код никуда не пишется, плата уходит в обычную
 * перезагрузку.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_power.h>

#include "dfu.h"

LOG_MODULE_REGISTER(dfu, LOG_LEVEL_INF);

#define RST_UF2 0x57
#define POLL_MS 50
#define TOUCH_BAUD 1200

static const char cmd[] = "dfu!";

static void dfu_reboot(const char *why)
{
	LOG_INF("уход в бутлоадер (%s)", why);
	k_msleep(50); /* дать логу вылезти в USB до перезагрузки */
	nrf_power_gpregret_set(NRF_POWER, 0, RST_UF2);
	sys_reboot(SYS_REBOOT_COLD);
}

static void dfu_watch(void *a, void *b, void *c)
{
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	uint8_t ch;
	int matched = 0;

	while (1) {
		while (uart_poll_in(console, &ch) == 0) {
			/* Совпадение подряд: мусор от зондирования сбрасывает счёт. */
			matched = (ch == cmd[matched]) ? matched + 1
				: (ch == cmd[0] ? 1 : 0);

			if (matched == sizeof(cmd) - 1) {
				dfu_reboot("команда 'dfu!' с хоста");
			}
		}

		uint32_t baud = 0;

		if (uart_line_ctrl_get(console, UART_LINE_CTRL_BAUD_RATE, &baud) == 0 &&
		    baud == TOUCH_BAUD) {
			dfu_reboot("касание 1200 бод");
		}

		k_msleep(POLL_MS);
	}
}

K_THREAD_STACK_DEFINE(dfu_stack, 768);
static struct k_thread dfu_thread;

void dfu_watch_start(void)
{
	k_thread_create(&dfu_thread, dfu_stack, K_THREAD_STACK_SIZEOF(dfu_stack),
			dfu_watch, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO,
			0, K_NO_WAIT);
	k_thread_name_set(&dfu_thread, "dfu");
	LOG_INF("бутлоадер по команде: 'dfu!' в консоль или 1200 бод");
}
