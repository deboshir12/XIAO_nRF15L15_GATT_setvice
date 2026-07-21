cmake_minimum_required(VERSION 3.20.0)

if(NOT DEFINED DTC_OVERLAY_FILE)
  if(BOARD STREQUAL "ME25ls02/nrf54l15/cpuapp")
    set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/boards/ME25ls02_nrf54l15_cpuapp.overlay" CACHE STRING "")
  else()
    set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/app.overlay" CACHE STRING "")
  endif()
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ble_gatt_service_try)

target_sources(app PRIVATE src/main.c src/emf_data.c src/emf_spi.c)

if(BOARD STREQUAL "ME25ls02/nrf54l15/cpuapp")
  target_compile_definitions(app PRIVATE
    EMF_SPIS_SCK_PORT=2
    EMF_SPIS_SCK_PIN_NUM=6
    EMF_SPIS_MOSI_PORT=2
    EMF_SPIS_MOSI_PIN_NUM=8
    EMF_SPIS_CSN_PORT=2
    EMF_SPIS_CSN_PIN_NUM=10
    EMF_SPIS_BOARD_NAME="ME25"
  )
else()
  target_compile_definitions(app PRIVATE
    EMF_SPIS_BOARD_NAME="XIAO"
  )
endif()
