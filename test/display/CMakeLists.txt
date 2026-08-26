# Отдельный стенд для проверки экрана: чистое Zephyr-приложение без ZMK.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(display_test)

target_sources(app PRIVATE src/patterns.c src/board.c ${CMAKE_CURRENT_SOURCE_DIR}/../common/dfu.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# С драйвером ls0xx кадры идут через display API, без него — свой протокол.
if(CONFIG_LS0XX)
  target_sources(app PRIVATE src/main.c)
else()
  target_sources(app PRIVATE src/raw.c)
endif()
