# frank-sdk — pick the board. Include BEFORE pico_sdk_import.cmake:
#
#   set(FRANK_BOARD M2 CACHE STRING "Board")      # or -DFRANK_BOARD=NX
#   include(frank-sdk/cmake/board.cmake)
#   include(frank-sdk/pico_sdk_import.cmake)
#   project(myfw C CXX ASM)
#   pico_sdk_init()
#   add_subdirectory(frank-sdk)
#   add_executable(myfw main.c)
#   frank_firmware(myfw)
#
# The board's facts come from boards/gen/boards.json (tools/gen_boards.py),
# the same data the C headers are generated from.
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.19)   # string(JSON)

get_filename_component(FRANK_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(READ "${FRANK_SDK_DIR}/boards/gen/boards.json" _frank_boards)
string(JSON _n LENGTH "${_frank_boards}")
set(FRANK_BOARDS "")
math(EXPR _last "${_n} - 1")
foreach(_i RANGE ${_last})
    string(JSON _id MEMBER "${_frank_boards}" ${_i})
    list(APPEND FRANK_BOARDS ${_id})
endforeach()

if(NOT DEFINED FRANK_BOARD OR FRANK_BOARD STREQUAL "")
    message(FATAL_ERROR "frank-sdk: set FRANK_BOARD to one of: ${FRANK_BOARDS}")
endif()
string(TOUPPER "${FRANK_BOARD}" FRANK_BOARD)
if(NOT FRANK_BOARD IN_LIST FRANK_BOARDS)
    message(FATAL_ERROR "frank-sdk: FRANK_BOARD=${FRANK_BOARD} is not one of: ${FRANK_BOARDS}")
endif()
set_property(CACHE FRANK_BOARD PROPERTY STRINGS ${FRANK_BOARDS})

# what CMake needs to know of the board
macro(_frank_board_get var)
    string(JSON ${var} ERROR_VARIABLE _err GET "${_frank_boards}" ${FRANK_BOARD} ${ARGN})
    if(_err)
        set(${var} "")
    endif()
endmacro()
_frank_board_get(FRANK_BOARD_NAME name)
_frank_board_get(FRANK_BOARD_MCU mcu)
_frank_board_get(FRANK_BOARD_ROLE role)
_frank_board_get(FRANK_BOARD_CONSOLE_DEFAULT console default)
_frank_board_get(FRANK_BOARD_VIDEO_BACKEND video hdmi backend)
_frank_board_get(FRANK_BOARD_VGA_BASE video vga base)
_frank_board_get(FRANK_BOARD_SD_MODE sd mode)
_frank_board_get(FRANK_BOARD_USB_NATIVE usb native)
_frank_board_get(FRANK_BOARD_PAIR pair)

set(FRANK_CONSOLE "${FRANK_BOARD_CONSOLE_DEFAULT}" CACHE STRING "Console: usb (CDC on the native port), uart or none")
set_property(CACHE FRANK_CONSOLE PROPERTY STRINGS usb uart none)
_frank_board_get(_rt runtime)
if(_rt STREQUAL "ON" OR _rt STREQUAL "true" OR _rt STREQUAL "1")
    set(_rt_default ON)
else()
    set(_rt_default OFF)
endif()
option(FRANK_RUNTIME_BOARD "The firmware finds its board at run time: nothing board-specific at boot" ${_rt_default})

_frank_board_get(FRANK_BOARD_PIO_USB_DP usb pio_usb_dp)
_frank_board_get(FRANK_BOARD_PIO_PIO_USB pio pio_usb)
_frank_board_get(_ps2 ps2 kbd_clk)
_frank_board_get(_nes nespad clk)
set(FRANK_BOARD_HAS_PS2 OFF)
set(FRANK_BOARD_HAS_NESPAD OFF)
if(NOT _ps2 STREQUAL "")
    set(FRANK_BOARD_HAS_PS2 ON)
endif()
if(NOT _nes STREQUAL "")
    set(FRANK_BOARD_HAS_NESPAD ON)
endif()

# The USB host for frank_input: native (the board's USB port), pio (PIO-USB on
# usb.pio_usb_dp), or none. The native port cannot be the host and the CDC
# console at once: with the USB console the default is PIO-USB where the board
# has it, else none; with the UART console, the native port.
if(FRANK_BOARD_PIO_USB_DP AND (FRANK_CONSOLE STREQUAL "usb" OR NOT FRANK_BOARD_USB_NATIVE STREQUAL "host"))
    set(_host_default pio)
elseif(FRANK_BOARD_USB_NATIVE STREQUAL "host" AND FRANK_CONSOLE STREQUAL "uart")
    set(_host_default native)
else()
    set(_host_default none)
endif()
set(FRANK_USB_HOST "${_host_default}" CACHE STRING "USB host: native, pio or none")
set_property(CACHE FRANK_USB_HOST PROPERTY STRINGS native pio none)
if(FRANK_USB_HOST STREQUAL "native" AND FRANK_CONSOLE STREQUAL "usb")
    message(FATAL_ERROR "frank-sdk: the native USB port cannot be the host and the console: FRANK_CONSOLE=uart, or FRANK_USB_HOST=pio")
endif()
if(FRANK_USB_HOST STREQUAL "pio")
    if(NOT FRANK_BOARD_PIO_USB_DP)
        message(FATAL_ERROR "frank-sdk: ${FRANK_BOARD} has no PIO-USB pins (boards/*.yaml usb.pio_usb_dp)")
    endif()
    # Pico-PIO-USB (sekigon-gonnoc, MIT): TinyUSB's rp2040 family script picks it
    # up from here at pico_sdk_init() and makes tinyusb_pico_pio_usb (frank-dos)
    set(PICO_PIO_USB_PATH ${FRANK_SDK_DIR}/drivers/input/pio_usb)
endif()

string(TOLOWER "${FRANK_BOARD}" _bl)
set(PICO_PLATFORM rp2350 CACHE STRING "" FORCE)
set(PICO_BOARD "frank_${_bl}" CACHE STRING "Pico board type" FORCE)
set(PICO_BOARD_HEADER_DIRS "${FRANK_SDK_DIR}/boards/gen" CACHE PATH "" FORCE)

message(STATUS "frank-sdk: board ${FRANK_BOARD} (${FRANK_BOARD_NAME}), ${FRANK_BOARD_MCU}, console ${FRANK_CONSOLE}, USB host ${FRANK_USB_HOST}")
