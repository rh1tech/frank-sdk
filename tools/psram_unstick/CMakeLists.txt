# frank-sdk: psram_unstick — a RAM-only program (writes no flash) that drives the
# PSRAM chip select high and returns to BOOTSEL. For a board whose flash reads
# back noisy in BOOTSEL because the PSRAM is stuck driving the shared QSPI bus
# (frank-react, 2026-11-17). `frank unstick BOARD` builds and runs it.
cmake_minimum_required(VERSION 3.19)
set(FRANK_BOARD "M2" CACHE STRING "Board")
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake)
include(${FRANK_SDK_DIR}/pico_sdk_import.cmake)
project(psram_unstick C CXX ASM)
pico_sdk_init()
add_executable(psram_unstick main.c)
pico_set_binary_type(psram_unstick no_flash)
target_link_libraries(psram_unstick pico_stdlib)
pico_enable_stdio_usb(psram_unstick 0)
pico_enable_stdio_uart(psram_unstick 0)
string(TOLOWER "${FRANK_BOARD}" _bl)
set_target_properties(psram_unstick PROPERTIES OUTPUT_NAME "${_bl}_psram_unstick")
pico_add_extra_outputs(psram_unstick)
