cmake_minimum_required(VERSION 3.13)
set(PICO_SDK_PATH $ENV{PICO_SDK_PATH} CACHE PATH "Path to Raspberry Pi Pico SDK")

include(pico_sdk_import.cmake)
project(amplified_mouse)
pico_sdk_init()

add_executable(amplified_mouse
  src/main.c
  src/settings.c
  src/usb_descriptors.c
)

target_include_directories(amplified_mouse PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${CMAKE_CURRENT_LIST_DIR}/config
)

target_link_libraries(amplified_mouse PRIVATE
  pico_stdlib
  pico_unique_id
  hardware_flash
  tinyusb_device
  tinyusb_board
)

pico_add_extra_outputs(amplified_mouse)
