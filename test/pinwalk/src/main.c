/*
 * Пинвокер: SPI нет вообще, каждый пин по очереди отдаёт меандр своей
 * частоты. По частоте в трейсе логического анализатора однозначно видно,
 * какой провод в какой канал воткнут — и выходит ли сигнал из платы вообще.
 *
 * Режим по умолчанию — три пина экрана: SCK P0.20 (D3), MOSI P0.17 (D2),
 * CS P0.06 (D1). Режим ALL (`make pinwalk` и переменная в этом файле) гонит
 * весь разъём pro_micro: если провод воткнут не туда, он всё равно отзовётся,
 * и по частоте видно, на каком пине сидит.
 *
 * Частоты разнесены так, чтобы период читался в трейсе без декодера:
 * пин номер i отдаёт (i + 1) * 500 Гц.
 *
 * P0.13 всё время высоко — это ключ пада VCC, иначе панель не питается.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include "dfu.h"

LOG_MODULE_REGISTER(pinwalk, LOG_LEVEL_INF);

#define EXT_POWER_PIN 13
#define BURST_MS 700
#define GAP_MS 300
#define TRIO_MS 10000
#define TRIO_TICK_US 25

struct walk_pin {
	uint8_t pin;      /* номер пина; бит P1 = порт gpio1 */
	const char *name; /* подпись на плате */
};

/*
 * Весь разъём pro_micro по карте arduino_pro_micro_pins.dtsi. Пины на gpio1
 * помечены флагом: у nice!nano половина разъёма сидит на втором порту.
 * Частота = (индекс + 1) * 500 Гц, поэтому пин опознаётся по периоду.
 */
#define P1 0x80

static const struct walk_pin pins[] = {
	{ 8,       "D0  P0.08" },
	{ 6,       "D1  P0.06 (CS экрана)" },
	{ 17,      "D2  P0.17 (MOSI экрана)" },
	{ 20,      "D3  P0.20 (SCK экрана)" },
	{ 22,      "D4  P0.22" },
	{ 24,      "D5  P0.24" },
	{ P1 | 0,  "D6  P1.00" },
	{ 11,      "D7  P0.11" },
	{ P1 | 4,  "D8  P1.04" },
	{ P1 | 6,  "D9  P1.06" },
	{ 9,       "D10 P0.09" },
	{ 10,      "D16 P0.10" },
	{ P1 | 11, "D14 P1.11" },
	{ P1 | 13, "D15 P1.13" },
	{ P1 | 15, "D18 P1.15" },
	{ 2,       "D19 P0.02" },
	{ 29,      "D20 P0.29" },
	{ 31,      "D21 P0.31" },
};

static void wait_for_console(void)
{
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	uint32_t dtr = 0;

	for (int i = 0; i < 100 && !dtr; i++) {
		uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}
	k_sleep(K_MSEC(100));
}

#define PORT(p) ((p)->pin & P1 ? gpio1 : gpio0)
#define PIN(p) ((p)->pin & ~P1)

/*
 * Фаза «трио»: три пина экрана машут одновременно и непрерывно, каждый своей
 * частотой (SCK 1 кГц, MOSI 2 кГц, CS 4 кГц). Так провод прозванивается в
 * любой момент — не нужно ловить фазу обхода. Общий тик 25 мкс: полупериоды
 * выходят целыми (20, 10 и 5 тиков).
 */
static void trio_phase(const struct device *gpio0)
{
	const uint8_t pin[3] = { 20, 17, 6 };
	const int half[3] = { 20, 10, 5 };
	int level[3] = { 0, 0, 0 };
	int ticks = TRIO_MS * 1000 / TRIO_TICK_US;

	LOG_INF("=== трио: SCK P0.20 1 кГц, MOSI P0.17 2 кГц, CS P0.06 4 кГц, %d мс",
		TRIO_MS);

	for (int t = 0; t < ticks; t++) {
		for (int i = 0; i < 3; i++) {
			if (t % half[i] == 0) {
				level[i] = !level[i];
				gpio_pin_set(gpio0, pin[i], level[i]);
			}
		}
		k_busy_wait(TRIO_TICK_US);
	}

	for (int i = 0; i < 3; i++) {
		gpio_pin_set(gpio0, pin[i], 0);
	}
}

int main(void)
{
	const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));

	wait_for_console();

	if (!device_is_ready(gpio0) || !device_is_ready(gpio1)) {
		LOG_ERR("gpio не готов");
		return -ENODEV;
	}

	dfu_watch_start();

	gpio_pin_configure(gpio0, EXT_POWER_PIN, GPIO_OUTPUT_ACTIVE);
	LOG_INF("питание пада VCC включено (P0.%d высоко)", EXT_POWER_PIN);

	for (int i = 0; i < ARRAY_SIZE(pins); i++) {
		const struct walk_pin *p = &pins[i];

		gpio_pin_configure(PORT(p), PIN(p), GPIO_OUTPUT_INACTIVE);
	}

	while (1) {
		trio_phase(gpio0);

		LOG_INF("=== проход по разъёму, частота = (индекс + 1) * 500 Гц");

		for (int i = 0; i < ARRAY_SIZE(pins); i++) {
			const struct walk_pin *p = &pins[i];
			int hz = (i + 1) * 500;
			int half_us = 500000 / hz;
			int cycles = hz * BURST_MS / 1000;

			LOG_INF("[%2d] %-24s %5d Гц", i, p->name, hz);

			for (int c = 0; c < cycles; c++) {
				gpio_pin_set(PORT(p), PIN(p), 1);
				k_busy_wait(half_us);
				gpio_pin_set(PORT(p), PIN(p), 0);
				k_busy_wait(half_us);
			}

			/* Пауза-разделитель: в трейсе видно границу пинов. */
			k_msleep(GAP_MS);
		}
	}

	return 0;
}
