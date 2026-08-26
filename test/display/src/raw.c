/*
 * Стенд без драйвера: протокол Sharp реализован здесь, поэтому под контролем
 * всё сразу — тайминги CS, инверсия VCOM, состав посылки.
 *
 * Зачем: апстримный ls0xx берёт шину с delay = 0 (CS впритык к тактам) и
 * гоняет VCOM отдельным потоком, который дёргает тот же пин. Разделить эти
 * два фактора через devicetree нельзя, а здесь можно.
 *
 * Протокол (LS0XX, serial VCOM):
 *   CS высоко -> команда 0x01|VCOM -> для каждой строки: номер (с 1),
 *   W/8 байт данных, байт-пустышка -> завершающий байт -> CS низко.
 *   Панель применяет кадр по спаду CS. Без периодической инверсии VCOM
 *   она залипает от постоянной составляющей за десяток секунд.
 *
 * Прогон перебирает два пина в роли CS (D1 по адаптеру и D0) на пяти составах
 * посылки, каждый шаг начинается командой CLEAR. Счётчик в левом верхнем углу
 * кадра = номер шага, порядок совпадает с логом.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "board.h"
#include "patterns.h"

LOG_MODULE_REGISTER(display_test, LOG_LEVEL_DBG);

#define DISP   DT_CHOSEN(zephyr_display)
#define WIDTH  DT_PROP(DISP, width)
#define HEIGHT DT_PROP(DISP, height)
#define STRIDE (WIDTH / 8)

#define CMD_WRITE 0x01
#define CMD_VCOM  0x02
#define CMD_CLEAR 0x04

/* Запас по таймингам CS: у Sharp хватает единиц микросекунд, берём с лихвой. */
#define CS_SETUP_US 200
#define CS_HOLD_US  200
#define CS_LOW_US   200

#define VCOM_INTERVAL_MS 33

/* Чем режим отличается от драйверной посылки. */
struct mode {
	const char *name;
	bool vcom_in_cmd;   /* бит VCOM в команде кадра */
	bool vcom_pulses;   /* пустые команды между кадрами */
	uint8_t dummy;      /* байт-пустышка после строки */
	uint8_t trailing;   /* завершающий байт кадра */
	bool single_xfer;   /* весь кадр одной посылкой */
};

/*
 * Составы посылки: отличия моего кода от драйверного. Проверялись на
 * непитаемой панели, поэтому перебор нужен заново.
 */
static const struct mode modes[] = {
	{ "полный VCOM", true, true, 27, CMD_WRITE, false },
	{ "VCOM только в команде", true, false, 27, CMD_WRITE, false },
	{ "без VCOM (как -mcs)", false, false, 27, CMD_WRITE, false },
	{ "без VCOM, пустышки нулями", false, false, 0, 0, false },
	{ "без VCOM, одной посылкой", false, false, 27, CMD_WRITE, true },
};

/* Пины: штатный по адаптеру и сосед, на который падало подозрение. */
static const int probe_pins[] = { 1, 0 };

static const struct device *const spi = DEVICE_DT_GET(DT_BUS(DISP));

/* SPI_LOCK_ON — как у драйвера: шина держится за нами весь кадр. */
static struct spi_config spi_cfg = {
	.frequency = DT_PROP(DISP, spi_max_frequency),
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LOCK_ON,
};

/*
 * CS держим сами (cs-gpios с шины снят оверлеем) и перебираем: в прогоне с
 * ручным CS на D1 кадры проходили ровно пока был поднят D0, то есть провод
 * припаян не туда, куда его ждёт nice_view_adapter. Пины шины пропущены:
 * D2 (P0.17, MOSI) и D3 (P0.20, SCK).
 */
struct cand {
	const struct device *port;
	gpio_pin_t pin;
	const char *name;
};

#define CAND(dev, p, n) { DEVICE_DT_GET(DT_NODELABEL(dev)), p, n }

static const struct cand cands[] = {
	CAND(gpio0, 8, "D0 P0.08"),   CAND(gpio0, 6, "D1 P0.06"),
	CAND(gpio0, 22, "D4 P0.22"),  CAND(gpio0, 24, "D5 P0.24"),
	CAND(gpio1, 0, "D6 P1.00"),   CAND(gpio0, 11, "D7 P0.11"),
	CAND(gpio1, 4, "D8 P1.04"),   CAND(gpio1, 6, "D9 P1.06"),
	CAND(gpio0, 9, "D10 P0.09"),  CAND(gpio0, 10, "D16 P0.10"),
	CAND(gpio1, 11, "D14 P1.11"), CAND(gpio1, 13, "D15 P1.13"),
	CAND(gpio1, 15, "D18 P1.15"), CAND(gpio0, 2, "D19 P0.02"),
	CAND(gpio0, 29, "D20 P0.29"), CAND(gpio0, 31, "D21 P0.31"),
};

/* Текущий кандидат в роли CS. */
static const struct cand *cs = &cands[0];

static const struct gpio_dt_spec led =
	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(blue_led), gpios, {0});

static uint8_t fb[STRIDE * HEIGHT];
static uint8_t line[STRIDE + 2];
static uint8_t tx[1 + HEIGHT * (STRIDE + 2) + 1];
static bool vcom;

static int put(const uint8_t *buf, size_t len)
{
	struct spi_buf b = { .buf = (void *)buf, .len = len };
	struct spi_buf_set set = { .buffers = &b, .count = 1 };

	return spi_write(spi, &spi_cfg, &set);
}

static void cs_begin(void)
{
	gpio_pin_set(cs->port, cs->pin, 1);
	k_busy_wait(CS_SETUP_US);
}

static void cs_end(void)
{
	k_busy_wait(CS_HOLD_US);
	gpio_pin_set(cs->port, cs->pin, 0);
	spi_release(spi, &spi_cfg);
	k_busy_wait(CS_LOW_US);
}

static uint8_t vcom_bit(void)
{
	uint8_t bit = vcom ? CMD_VCOM : 0;

	vcom = !vcom;
	return bit;
}

/* Пустая команда: только инверсия VCOM, кадр не трогает. */
static int vcom_pulse(void)
{
	uint8_t cmd[2] = { vcom_bit(), 0 };
	int rc;

	cs_begin();
	rc = put(cmd, sizeof(cmd));
	cs_end();

	return rc;
}

/* Команда полной очистки: некоторые панели ждут её перед первым кадром. */
static int clear(void)
{
	uint8_t cmd[2] = { CMD_CLEAR, 0 };
	int rc;

	cs_begin();
	rc = put(cmd, sizeof(cmd));
	cs_end();

	return rc;
}

static int flush(const struct mode *m)
{
	uint8_t cmd = CMD_WRITE | (m->vcom_in_cmd ? vcom_bit() : 0);
	uint8_t trailing = m->trailing;
	int rc = 0;

	if (m->single_xfer) {
		size_t n = 0;

		tx[n++] = cmd;
		for (int y = 0; y < HEIGHT; y++) {
			tx[n++] = y + 1;
			memcpy(&tx[n], &fb[y * STRIDE], STRIDE);
			n += STRIDE;
			tx[n++] = m->dummy;
		}
		tx[n++] = trailing;

		cs_begin();
		rc = put(tx, n);
		cs_end();

		return rc;
	}

	/* Отдельными посылками — ровно как ls0xx. */
	cs_begin();
	rc |= put(&cmd, sizeof(cmd));

	for (int y = 0; y < HEIGHT; y++) {
		line[0] = y + 1;
		memcpy(&line[1], &fb[y * STRIDE], STRIDE);
		line[STRIDE + 1] = m->dummy;
		rc |= put(line, sizeof(line));
	}

	rc |= put(&trailing, sizeof(trailing));
	cs_end();

	return rc;
}

/* Пауза; в режимах с VCOM — с инверсией, иначе панель залипает. */
static void hold(const struct mode *m, int ms)
{
	if (!m->vcom_pulses) {
		k_msleep(ms);
		return;
	}

	for (int t = 0; t < ms; t += VCOM_INTERVAL_MS) {
		k_msleep(VCOM_INTERVAL_MS);
		vcom_pulse();
	}
}

int main(void)
{
	wait_for_console();

	/* Первым делом питание: без него панель мертва, а логи об этом молчат. */
	ext_power_on();

	LOG_INF("стенд без драйвера: %dx%d, кадр %d байт", WIDTH, HEIGHT,
		(int)sizeof(fb));
	LOG_INF("SPI %d Гц, CS вручную (%d/%d/%d мкс), шагов %d",
		spi_cfg.frequency, CS_SETUP_US, CS_HOLD_US, CS_LOW_US,
		(int)(ARRAY_SIZE(probe_pins) * ARRAY_SIZE(modes)));

	if (!device_is_ready(spi)) {
		LOG_ERR("SPI не готов");
		return -ENODEV;
	}

	if (!device_is_ready(cs->port)) {
		LOG_ERR("порт CS не готов");
		return -ENODEV;
	}

	for (int i = 0; i < ARRAY_SIZE(cands); i++) {
		gpio_pin_configure(cands[i].port, cands[i].pin, GPIO_OUTPUT_INACTIVE);
	}
	if (led.port != NULL) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}

	LOG_INF("проба VCOM-команды на %s: %d", cs->name, vcom_pulse());

	/* Три структурных кадра на шаг: рамка, шахматка, линейка. */
	static const int frames[] = { 5, 4, 6 };

	for (int pass = 0;; pass++) {
		int step = 0;

		for (int p = 0; p < ARRAY_SIZE(probe_pins); p++) {
			cs = &cands[probe_pins[p]];

			for (int i = 0; i < ARRAY_SIZE(modes); i++) {
				const struct mode *m = &modes[i];

				step++;
				LOG_INF("=== шаг %d (счётчик %d): CS на %s, %s",
					step, step, cs->name, m->name);
				LOG_INF("   clear -> %d", clear());

				for (int k = 0; k < ARRAY_SIZE(frames); k++) {
					const char *name = pattern(fb, WIDTH, HEIGHT,
								   frames[k], 0);
					int rc;

					mark(fb, WIDTH, HEIGHT, step);
					rc = flush(m);

					LOG_INF("   %s -> %d", name, rc);
					if (rc != 0) {
						LOG_ERR("spi_write вернул %d", rc);
					}

					if (led.port != NULL) {
						gpio_pin_toggle_dt(&led);
					}

					hold(m, 2000);
				}
			}
		}
	}

	return 0;
}
