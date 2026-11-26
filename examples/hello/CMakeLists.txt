# frank-sdk example: hello. Build: cmake -B build -DFRANK_BOARD=M2 && cmake --build build
cmake_minimum_required(VERSION 3.19)

set(FRANK_BOARD "M2" CACHE STRING "Board")
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake)
include(${FRANK_SDK_DIR}/pico_sdk_import.cmake)

project(hello C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(${FRANK_SDK_DIR} frank-sdk)

add_executable(hello main.c)
frank_firmware(hello)
