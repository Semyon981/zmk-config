/*
 * Стенд экрана nice!view через штатный драйвер ls0xx. Никакого ZMK и LVGL:
 * кадр собирается руками и уходит в display_write.
 *
 * Что смотреть:
 *   - синий светодиод платы мигает — прошивка жива, цикл идёт;
 *   - в логе имя кадра и код возврата;
 *   - счётчик шага в левом верхнем углу кадра: по залипшей картинке видно,
 *     какой именно шаг её оставил.
 *
 * Вариант с ручным CS (`make display-test MANUAL=1`) забирает пин у SPI:
 * у драйвера в SPI_DT_SPEC_INST_GET зашит delay = 0, то есть CS поднимается
 * впритык к первому такту. Полный контроль и над CS, и над VCOM даёт только
 * сборка без драйвера — src/raw.c.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

#include "board.h"
#include "dfu.h"
#include "patterns.h"

LOG_MODULE_REGISTER(display_test, LOG_LEVEL_DBG);

#define DISP   DT_CHOSEN(zephyr_display)
#define BUS    DT_BUS(DISP)
#define WIDTH  DT_PROP(DISP, width)
#define HEIGHT DT_PROP(DISP, height)
#define STRIDE (WIDTH / 8)

static uint8_t buf[STRIDE * HEIGHT];

static const struct gpio_dt_spec led =
	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(blue_led), gpios, {0});

/*
 * CS панели: pro_micro 1 = D1 = P0.06. Если cs-gpios снят оверлеем, пином
 * владеет приложение и дёргает его с запасом по таймингам.
 */
#if DT_NODE_HAS_PROP(BUS, cs_gpios)
#define MANUAL_CS 0
static const struct gpio_dt_spec cs = GPIO_DT_SPEC_GET_BY_IDX(BUS, cs_gpios, 0);
#else
#define MANUAL_CS 1
static const struct gpio_dt_spec cs = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
	.pin = 6,
	.dt_flags = GPIO_ACTIVE_HIGH,
};
#endif

static struct display_buffer_descriptor desc = {
	.buf_size = sizeof(buf),
	.width = WIDTH,
	.height = HEIGHT,
	.pitch = WIDTH,
};

#if MANUAL_CS
/*
 * Как держать CS. «Постоянно высок» — единственный режим, в котором до панели
 * доходят и кадры, и пустые VCOM-команды драйверного потока: тот шлёт их в
 * произвольный момент, и опущенный между кадрами CS их бы съел. Цена — нет
 * спада CS после кадра, которым Sharp кадр применяет.
 */
enum cs_mode {
	CS_ALWAYS_HIGH,
	CS_PULSE_200US,
	CS_PULSE_2MS,
	CS_MODE_COUNT,
};

static const char *const cs_mode_name[] = {
	"CS постоянно высок",
	"CS вокруг кадра, 200 мкс",
	"CS вокруг кадра, 2 мс",
};

/*
 * Рабочий режим — импульс 2 мс: перебор показал, что чем длиннее выдержки на
 * CS, тем больше кадров доходит целиком, а с постоянно высоким CS панель не
 * применяет кадр вовсе (ей нужен спад). Сама эта зависимость от длительности
 * фронтов и есть признак плохой линии, а не софта.
 */
static enum cs_mode cs_mode = CS_PULSE_2MS;

/*
 * Инверсия VCOM своими руками: поток драйвера снят оверлеем, потому что при
 * кадре длиннее 100 мс он отдаёт семафор, которого не брал, и его пустая
 * команда влезает в середину кадра. Между кадрами шина свободна, так что
 * шлём ту же пустую команду сами, тем же CS.
 */
#define VCOM_INTERVAL_MS 33

static const struct device *const bus = DEVICE_DT_GET(BUS);

static struct spi_config vcom_cfg = {
	.frequency = DT_PROP(DISP, spi_max_frequency),
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8),
};

static bool vcom_state;

static void vcom_pulse(void)
{
	uint8_t cmd[2] = { vcom_state ? 0x02 : 0x00, 0 };
	struct spi_buf b = { .buf = cmd, .len = sizeof(cmd) };
	struct spi_buf_set set = { .buffers = &b, .count = 1 };
	bool pulse = cs_mode != CS_ALWAYS_HIGH;

	vcom_state = !vcom_state;

	if (pulse) {
		gpio_pin_set_dt(&cs, 1);
		k_busy_wait(200);
	}

	spi_write(bus, &vcom_cfg, &set);

	if (pulse) {
		k_busy_wait(200);
		gpio_pin_set_dt(&cs, 0);
	}
}

/* Пауза с инверсией VCOM: без неё панель залипает от постоянной составляющей. */
static void hold(int ms)
{
	for (int t = 0; t < ms; t += VCOM_INTERVAL_MS) {
		k_msleep(VCOM_INTERVAL_MS);
		vcom_pulse();
	}
}
#else
static void hold(int ms)
{
	k_msleep(ms);
}
#endif

static int write_frame(const struct device *dev)
{
#if MANUAL_CS
	int us = cs_mode == CS_PULSE_2MS ? 2000 : 200;
	int rc;

	if (cs_mode == CS_ALWAYS_HIGH) {
		gpio_pin_set_dt(&cs, 1);
		return display_write(dev, 0, 0, &desc, buf);
	}

	gpio_pin_set_dt(&cs, 1);
	k_busy_wait(us);
	rc = display_write(dev, 0, 0, &desc, buf);
	k_busy_wait(us);
	gpio_pin_set_dt(&cs, 0);
	k_busy_wait(us);

	return rc;
#else
	return display_write(dev, 0, 0, &desc, buf);
#endif
}

/* Меандр на CS в обход SPI: проверить тестером, что сигнал доходит. */
static void cs_square_wave(void)
{
	LOG_INF("меандр на CS (порт %s, пин %d), 3 с по 2 Гц",
		cs.port->name, cs.pin);

	for (int i = 0; i < 12; i++) {
		gpio_pin_set_dt(&cs, i % 2);
		k_sleep(K_MSEC(250));
	}
	gpio_pin_set_dt(&cs, 0);
}

#if MANUAL_CS
/*
 * Один и тот же кадр пять раз подряд — различитель причины рваности.
 * Достраивается от повтора к повтору: потери случайные, дело в целостности
 * линии. Каждый раз битый одинаково: систематика — формат посылки или
 * тайминги, а не провод.
 */
static void repeat_phase(const struct device *dev)
{
	LOG_INF("=== повтор одного кадра 5 раз (счётчик 4)");

	pattern(buf, WIDTH, HEIGHT, 6, 0);
	mark(buf, WIDTH, HEIGHT, 4);

	for (int i = 0; i < 5; i++) {
		int rc = write_frame(dev);

		LOG_INF("   повтор %d -> %d", i + 1, rc);
		hold(700);
	}

	hold(3000);
}
#endif

/*
 * Бегущая полоса — единственный кадр, на котором видно само обновление:
 * залипшую картинку от живой не отличить по статике.
 */
static void anim_phase(const struct device *dev)
{
	LOG_INF("=== бегущая полоса, 40 кадров");

	for (int i = 0; i < 40; i++) {
		pattern(buf, WIDTH, HEIGHT, PATTERN_COUNT - 1, i * 4);
		write_frame(dev);
		hold(100);
	}
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(DISP);
	struct display_capabilities caps;

	wait_for_console();
	dfu_watch_start();

	/* Первым делом питание: без него панель мертва, а логи об этом молчат. */
	ext_power_on();

	LOG_INF("стенд экрана: %dx%d, кадр %d байт", WIDTH, HEIGHT, (int)sizeof(buf));

	if (led.port != NULL) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	} else {
		LOG_WRN("светодиод не найден в девайстри");
	}

	if (!device_is_ready(dev)) {
		LOG_ERR("драйвер экрана не поднялся — дальше смысла нет");
		return -ENODEV;
	}

#if MANUAL_CS
	/* Настроить до меандра: пином теперь владеем мы, а не SPI-драйвер. */
	gpio_pin_configure_dt(&cs, GPIO_OUTPUT_INACTIVE);
	LOG_INF("CS ручной (%s), VCOM свой каждые %d мс",
		cs_mode_name[cs_mode], VCOM_INTERVAL_MS);
#endif

	cs_square_wave();

	display_get_capabilities(dev, &caps);
	LOG_INF("драйвер: %dx%d, форматы 0x%02x, текущий 0x%02x, screen_info 0x%02x",
		caps.x_resolution, caps.y_resolution, caps.supported_pixel_formats,
		caps.current_pixel_format, caps.screen_info);

	/* На nice!view пин DISP притянут на плате, ответ "Unsupported" — норма. */
	LOG_INF("blanking_off: %d", display_blanking_off(dev));

	/* Структурные кадры: рамка, шахматка, линейка, полосы. */
	static const int frames[] = { 5, 4, 6, 2 };

	for (int pass = 0;; pass++) {
		for (int k = 0; k < ARRAY_SIZE(frames); k++) {
			const char *name = pattern(buf, WIDTH, HEIGHT, frames[k], 0);
			int rc;

			mark(buf, WIDTH, HEIGHT, k + 1);
			rc = write_frame(dev);

			LOG_INF("кадр (счётчик %d): %s -> %d", k + 1, name, rc);

			if (led.port != NULL) {
				gpio_pin_toggle_dt(&led);
			}

			hold(2500);
		}

		anim_phase(dev);
#if MANUAL_CS
		repeat_phase(dev);
#endif
	}

	return 0;
}
