BOARD  := nice_nano@2.0.0/nrf52840/zmk
CONFIG := $(CURDIR)/config
SIDES  := left right

west-build = west build -p auto -s zmk/app -d build/$(1) -b '$(BOARD)' -- \
	-DSHIELD=$(2) -DZMK_CONFIG='$(CONFIG)'

.PHONY: all $(SIDES) reset flash clean

all: $(SIDES)

$(SIDES):
	$(call west-build,$@,corne_$@)

reset:
	$(call west-build,$@,settings_reset)

flash-%:
	./scripts/flash.sh build/$*/zephyr/zmk.uf2

flash: flash-left flash-right
	

clean:
	rm -rf build


# export ZEPHYR_TOOLCHAIN_VARIANT=zephyr