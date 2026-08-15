if(CONFIG_ZMK_RU_LAYER_SYNC)
  zephyr_library()
  # Заголовки ZMK лежат в самом приложении, а не в его модуле, и в общий
  # include-путь не попадают — добавляем явно.
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(src/ru_layer_sync.c)
endif()
