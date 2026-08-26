# Пинвокер: дёргает пины SPI по очереди уникальными частотами.
# Нужен, чтобы сопоставить провода логического анализатора с пинами платы.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pinwalk)

target_sources(app PRIVATE src/main.c ${CMAKE_CURRENT_SOURCE_DIR}/../common/dfu.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
