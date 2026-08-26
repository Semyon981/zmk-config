# Условие на центральную половину стоит здесь, а не в Kconfig: на периферии
# keymap не собирается и zmk_keymap_* не линкуется, но выразить это зависимостью
# нельзя — общий для двух половинок .conf раскладки ругался бы на присваивание y.
if(CONFIG_ZMK_RU_LAYER_SYNC AND (NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
  zephyr_library()
  # Заголовки ZMK лежат в самом приложении, а не в его модуле, и в общий
  # include-путь не попадают — добавляем явно.
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(src/ru_layer_sync.c)
endif()
