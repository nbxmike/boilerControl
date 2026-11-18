cmake_minimum_required(VERSION 3.20.0)

if(NOT BOARD)
  set(BOARD esp32_devkitc/esp32/procpu)
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/config_secrets.h")
  message(FATAL_ERROR "Copy include/sample_cfg_secrets.include to include/config_secrets.h and configure credentials")
endif()

find_package(Zephyr 4.5.0 REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermostat_router LANGUAGES C)
target_include_directories(app PRIVATE include)
target_sources(app PRIVATE src/main.c src/network.c src/router.c)
