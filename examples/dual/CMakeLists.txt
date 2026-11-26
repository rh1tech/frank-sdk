# frank-sdk example: dual — the two halves of FRANK Next / Core 2 over the link.
# Build both: -DFRANK_BOARD=NX (or C2) for the master, -DFRANK_BOARD=SL for the slave.
cmake_minimum_required(VERSION 3.19)

set(FRANK_BOARD "NX" CACHE STRING "Board: NX or C2 (master), SL (slave)")
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake)
include(${FRANK_SDK_DIR}/pico_sdk_import.cmake)

project(dual C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(${FRANK_SDK_DIR} frank-sdk)

add_executable(dual main.c)
target_link_libraries(dual frank_link)
frank_firmware(dual)
