#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include "board.h"

LOG_MODULE_REGISTER(board, LOG_LEVEL_INF);

#define EXT_POWER_PIN 13
#define EXT_POWER_DELAY_MS 50

int ext_power_on(void)
{
	const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	int rc;

	if (!device_is_ready(gpio0)) {
		LOG_ERR("gpio0 не готов");
		return -ENODEV;
	}

	rc = gpio_pin_configure(gpio0, EXT_POWER_PIN, GPIO_OUTPUT_ACTIVE);
	if (rc < 0) {
		LOG_ERR("не поднять P0.%d: %d", EXT_POWER_PIN, rc);
		return rc;
	}

	k_msleep(EXT_POWER_DELAY_MS);
	LOG_INF("внешнее питание включено (P0.%d высоко)", EXT_POWER_PIN);

	return 0;
}

void wait_for_console(void)
{
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	uint32_t dtr = 0;

	for (int i = 0; i < 100 && !dtr; i++) {
		uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}
	k_sleep(K_MSEC(100));
}
