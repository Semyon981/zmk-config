# Половинки собираются разными прошивками и показывают разные экраны, поэтому
# роль выбирается здесь, а не #if'ами внутри файла: на периферии нет ни keymap,
# ни WPM, ни endpoint'ов, и центральный виджет туда просто не слинкуется.
#
# Условие на CONFIG_ZMK_LANG_SCREEN — в CMake, а не только в Kconfig: .conf
# раскладки общий для обеих половинок, см. CLAUDE.md.
if(CONFIG_ZMK_LANG_SCREEN)
  zephyr_library()
  # Заголовки ZMK лежат в самом приложении, а не в его модуле.
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(src/canvas.c)
  zephyr_library_sources(src/status_screen.c)
  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(src/central.c)
  else()
    zephyr_library_sources(src/peripheral.c)
  endif()
endif()
