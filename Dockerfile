FROM zmkfirmware/zmk-build-arm:stable

WORKDIR /workspace

COPY config/west.yml config/west.yml

RUN git config --system --add safe.directory '*'

RUN west init -l config \
	&& west update --narrow -o=--depth=1

RUN west config zephyr.base zephyr

ENV CMAKE_PREFIX_PATH=/workspace/zephyr

RUN mkdir -p /build/.cache && chmod -R 777 /build
