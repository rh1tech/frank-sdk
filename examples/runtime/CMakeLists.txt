# frank-sdk example: runtime — a firmware that finds its board at run time.
# Build: -DFRANK_BOARD=GB (or GA), or any target with -DFRANK_RUNTIME_BOARD=ON
cmake_minimum_required(VERSION 3.19)

set(FRANK_BOARD "GB" CACHE STRING "Board")
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake)
include(${FRANK_SDK_DIR}/pico_sdk_import.cmake)

project(runtime C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(${FRANK_SDK_DIR} frank-sdk)

add_executable(runtime main.c)
target_link_libraries(runtime frank_fleet)
frank_firmware(runtime)
