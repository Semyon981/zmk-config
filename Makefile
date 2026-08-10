# Сборка прошивки в докере.
#
#   make            обе половинки
#   make left       одна половинка
#   make reset      прошивка сброса настроек
#   make flash      залить обе половинки (хост, USB)
#   make shell      шелл внутри контейнера
#   make clean      снести артефакты сборки и кеш
#   make image      пересобрать образ вручную
#
# Готовые uf2 кладутся в build/, промежуточные объектники — в докер-том
# zmk-config-build, чтобы не сорить на хосте и не терять инкрементальность.

IMAGE  := zmk-config-build
VOLUME := zmk-config-build
BOARD  := nice_nano@2.0.0/nrf52840/zmk
SIDES  := left right

DOCKER_RUN = docker run --rm \
	--user $$(id -u):$$(id -g) \
	-e HOME=/tmp -e XDG_CACHE_HOME=/build/.cache \
	-v '$(CURDIR)/config':/workspace/config:ro \
	-v '$(CURDIR)/build':/out \
	-v $(VOLUME):/build \
	-w /workspace

# $(1) — имя сборки, $(2) — шилд, $(3) — имя итогового uf2.
west-build = $(DOCKER_RUN) $(IMAGE) bash -c "west build -p auto -s zmk/app \
	-d /build/$(1) -b '$(BOARD)' -- -DSHIELD=$(2) -DZMK_CONFIG=/workspace/config \
	&& cp /build/$(1)/zephyr/zmk.uf2 /out/$(3).uf2"

.PHONY: all image dirs shell clean flash $(SIDES) reset

all: $(SIDES)

image:
	docker build -t $(IMAGE) .

# Каталог создаём на хосте заранее, иначе докер сделает его от root.
dirs:
	@mkdir -p build

$(SIDES) reset shell: image | dirs

$(SIDES):
	$(call west-build,$@,corne_$@,$@)

reset:
	$(call west-build,reset,settings_reset,reset)

shell:
	$(DOCKER_RUN) -it $(IMAGE) bash

flash-%:
	./scripts/flash.sh build/$*.uf2

flash: flash-left flash-right

clean:
	rm -rf build
	-docker volume rm $(VOLUME)
