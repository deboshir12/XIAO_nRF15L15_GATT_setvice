cmake_minimum_required(VERSION 3.20.0)

set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/app.overlay" CACHE STRING "" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ble_gatt_service_try)

target_sources(app PRIVATE src/main.c src/emf_data.c src/emf_spi.c)

target_compile_definitions(app PRIVATE
  EMF_SPIS_BOARD_NAME="XIAO"
)
