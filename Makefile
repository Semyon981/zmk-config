# Сборка прошивки в докере.
#
#   make            обе половинки
#   make left       одна половинка
#   make layouts    список раскладок
#   make reset      прошивка сброса настроек
#   make display-test  стенд экрана без ZMK (test/display)
#   make flash      залить обе половинки (хост, USB)
#   make shell      шелл внутри контейнера
#   make clean      снести артефакты сборки и кеш
#   make image      пересобрать образ вручную
#
# Экран по умолчанию — nice!view; SCREEN=oled собирает под штатный OLED
# (SSD1306). После смены экрана нужен pristine — make clean.
#
# LOG=1 собирает отладочный вариант — логи в USB-порт (/dev/ttyACM*) и
# отладочный уровень драйвера экрана: `make LOG=1 left` -> build/<...>-log.uf2.
#
# Раскладка выбирается переменной LAYOUT: `make LAYOUT=instant`. Файлы лежат в
# config/layouts/<имя>.keymap, рядом одноимённый .conf с Kconfig-правками этой
# раскладки. У каждой раскладки свой каталог сборки и свой uf2 с префиксом в
# имени, поэтому кеши не конфликтуют и по имени файла видно, что прошиваешь.
#
# Готовые uf2 кладутся в build/, промежуточные объектники — в докер-том
# zmk-config-build, чтобы не сорить на хосте и не терять инкрементальность.

IMAGE  := zmk-config-build
VOLUME := zmk-config-build
BOARD  := nice_nano@2.0.0/nrf52840/zmk
SIDES  := left right
LAYOUT ?= parity

# Экран по умолчанию — nice!view (Sharp memory LCD). Шилд nice_view_adapter
# гасит SSD1306 и переводит 5-контактный разъём Corne на SPI, nice_view
# добавляет дисплей и свой status screen. Правки поверх шилда лежат в
# config/display.overlay: 250 кГц вместо 1 МГц, отключённый spi3 и CS на D0
# (P0.08) вместо шилдового D1 — так разведён разъём на этой плате, см.
# CLAUDE.md. Настройки LVGL под эту панель — в config/corne.conf.
#
# Штатный OLED собирается через `make SCREEN=oled`. Смена шилда требует
# pristine (`-p auto` её не ловит): снести каталог раскладки в томе или
# `make clean`.
#
# Переменная называется SCREEN, а не DISPLAY: DISPLAY есть в окружении (X11),
# и `?=` подхватывал бы оттуда `:1` прямо в список шилдов.
SCREEN ?= nice_view
DISPLAY_SHIELDS = $(if $(filter nice_view,$(SCREEN)),nice_view_adapter nice_view)

# Правки девайстри поверх шилда nice_view (частота SPI, отключение spi3).
# Докладываются только вместе с ним: без шилда узла &nice_view нет.
# EXTRA_DTC_OVERLAY_FILE применяется после шилдовых оверлеев,
# DTC_OVERLAY_FILE бы их заменил.
DISPLAY_ARGS = $(if $(DISPLAY_SHIELDS),-DEXTRA_DTC_OVERLAY_FILE=/workspace/config/display.overlay)

DOCKER_RUN = docker run --rm \
	--user $$(id -u):$$(id -g) \
	-e HOME=/tmp -e XDG_CACHE_HOME=/build/.cache \
	-v '$(CURDIR)/config':/workspace/config:ro \
	-v '$(CURDIR)/test':/workspace/test:ro \
	-v '$(CURDIR)/build':/out \
	-v $(VOLUME):/build \
	-w /workspace

# Локальные модули лежат в config/, потому что только он монтируется в контейнер.
MODULES := /workspace/config/modules/ru-layer-sync

# KEYMAP_FILE перебивает автопоиск `<шилд>.keymap` в ZMK_CONFIG, EXTRA_CONF_FILE
# докладывается поверх corne.conf и corne_<side>.conf.
# `make LOG=1 left` собирает отладочный вариант: snippet zmk-usb-logging
# (логи на /dev/ttyACM*) плюс config/debug.conf с уровнем DBG у драйвера
# экрана. EXTRA_CONF_FILE — список через ';'.
#
# Snippet и .conf — CACHE-переменные CMake, менять их в готовом каталоге
# нельзя, поэтому у отладочной сборки свой каталог и свой uf2 с суффиксом
# -log: видно, что залито, и обычная сборка не теряет инкрементальность.
DEBUG_CONF = $(if $(LOG),;/workspace/config/debug.conf)
LOG_SNIPPET = $(if $(LOG),-S zmk-usb-logging)
LOG_SUFFIX = $(if $(LOG),-log)

LAYOUT_ARGS = -DKEYMAP_FILE=/workspace/config/layouts/$(LAYOUT).keymap \
	-DEXTRA_CONF_FILE='/workspace/config/layouts/$(LAYOUT).conf$(DEBUG_CONF)'

# $(1) — каталог сборки, $(2) — шилд, $(3) — доп. аргументы, $(4) — имя uf2.
zmk-build = $(DOCKER_RUN) $(IMAGE) bash -c "west build -p auto -s zmk/app \
	-d /build/$(1) -b '$(BOARD)' $(LOG_SNIPPET) \
	-- -DSHIELD='$(2)' -DZMK_CONFIG=/workspace/config \
	-DZMK_EXTRA_MODULES='$(MODULES)' $(3) \
	&& cp /build/$(1)/zephyr/zmk.uf2 /out/$(4).uf2"

.PHONY: all image dirs shell clean flash layouts $(SIDES) reset display-test pinwalk

all: $(SIDES)

image:
	docker build -t $(IMAGE) .

# Каталог создаём на хосте заранее, иначе докер сделает его от root.
dirs:
	@mkdir -p build

layouts:
	@ls config/layouts/*.keymap | sed 's|.*/||; s|\.keymap$$||' \
		| sed 's|^$(LAYOUT)$$|& (по умолчанию)|'

$(SIDES) reset shell display-test pinwalk: image | dirs

$(SIDES):
	$(call zmk-build,$(LAYOUT)-$@$(LOG_SUFFIX),corne_$@ $(DISPLAY_SHIELDS),$(DISPLAY_ARGS) $(LAYOUT_ARGS),$(LAYOUT)-$@$(LOG_SUFFIX))

reset:
	$(call zmk-build,reset,settings_reset,,reset)

# Стенд для экрана: чистое Zephyr-приложение из test/display, без ZMK и LVGL.
# Плата берётся из zmk/app/module — это самостоятельный Zephyr-модуль с
# определением nice_nano (вариант /zmk и код ZMK не подключаются).
# Логи на /dev/ttyACM* даёт апстримный snippet cdc-acm-console.
# CS=low и SWAP=1 — варианты разводки для перебора, если картинки нет:
# первый переворачивает полярность CS, второй меняет SCK и MOSI местами.
# EXTRA_DTC_OVERLAY_FILE — CACHE-переменная, поэтому каталог и uf2 свои.
# Варианты для перебора: CS=low переворачивает полярность CS, SWAP=1 меняет
# SCK и MOSI, MANUAL=1 отдаёт CS приложению, RAW=1 выбрасывает драйвер ls0xx
# и шлёт протокол Sharp из src/raw.c (там же VCOM и тайминги CS),
# GEOM=128|144 подставляет другую геометрию панели, FREQ=125|62 замедляет шину,
# CSPIN=d0 переносит CS с D1 (P0.06) на D0 (P0.08) — так разведён разъём
# экрана на плате клавиатуры.
# EXTRA_DTC_OVERLAY_FILE и EXTRA_CONF_FILE — CACHE-переменные, поэтому у
# каждого варианта свой каталог сборки и свой uf2 с суффиксом.
DT_VARIANT = $(if $(filter low,$(CS)),/workspace/test/display/cs-low.overlay;)$(if $(filter d0,$(CSPIN)),/workspace/test/display/cs-d0.overlay;)$(if $(SWAP),/workspace/test/display/swap.overlay;)$(if $(or $(MANUAL),$(RAW)),/workspace/test/display/manual-cs.overlay;)$(if $(GEOM),/workspace/test/display/geom-$(GEOM).overlay;)$(if $(FREQ),/workspace/test/display/freq-$(FREQ).overlay)
DT_ARGS = $(if $(DT_VARIANT),-DEXTRA_DTC_OVERLAY_FILE='$(DT_VARIANT)')
CONF_ARGS = $(if $(RAW),-DEXTRA_CONF_FILE=/workspace/test/display/raw.conf)
DT_SUFFIX = $(if $(filter low,$(CS)),-cslow)$(if $(filter d0,$(CSPIN)),-csd0)$(if $(SWAP),-swap)$(if $(MANUAL),-mcs)$(if $(RAW),-raw)$(if $(GEOM),-$(GEOM))$(if $(FREQ),-$(FREQ)k)

display-test:
	$(DOCKER_RUN) $(IMAGE) bash -c "west build -p auto -s /workspace/test/display \
		-d /build/display-test$(DT_SUFFIX) -b 'nice_nano@2.0.0/nrf52840' \
		-S cdc-acm-console \
		-- -DBOARD_ROOT=/workspace/zmk/app/module \
		-DDTS_ROOT=/workspace/zmk/app/module $(DT_ARGS) $(CONF_ARGS) \
		&& cp /build/display-test$(DT_SUFFIX)/zephyr/zephyr.uf2 \
			/out/display-test$(DT_SUFFIX).uf2"

# Пинвокер: без SPI, каждый пин по очереди отдаёт меандр своей частоты
# (SCK 1 кГц, MOSI 2 кГц, CS 4 кГц). Нужен, чтобы сопоставить провода
# логического анализатора с пинами платы.
pinwalk:
	$(DOCKER_RUN) $(IMAGE) bash -c "west build -p auto -s /workspace/test/pinwalk \
		-d /build/pinwalk -b 'nice_nano@2.0.0/nrf52840' \
		-S cdc-acm-console \
		-- -DBOARD_ROOT=/workspace/zmk/app/module \
		-DDTS_ROOT=/workspace/zmk/app/module \
		&& cp /build/pinwalk/zephyr/zephyr.uf2 /out/pinwalk.uf2"

flash-%:
	./scripts/flash.sh build/$*.uf2

flash: flash-$(LAYOUT)-left flash-$(LAYOUT)-right

clean:
	rm -rf build
	-docker volume rm $(VOLUME)
