# {{NAME}} — {{DESCRIPTION}}
# SPDX-License-Identifier: {{LICENSE}}
#
# ./build.sh BOARD   (BOARD: {{BOARDS}}; FRANK_SDK may point at another frank-sdk)
cmake_minimum_required(VERSION 3.19)

set(FRANK_BOARD "{{BOARD0}}" CACHE STRING "Board: {{BOARDS}}")
if(NOT FRANK_SDK)
    set(FRANK_SDK ${CMAKE_CURRENT_LIST_DIR}/frank-sdk)
endif()
include(${FRANK_SDK}/cmake/board.cmake)
include(${FRANK_SDK_DIR}/pico_sdk_import.cmake)

project({{NAME_ID}} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_subdirectory(${FRANK_SDK_DIR} frank-sdk)

file(READ ${CMAKE_CURRENT_LIST_DIR}/version.txt FW_VERSION)
string(STRIP "${FW_VERSION}" FW_VERSION)

add_executable({{NAME_ID}} src/main.c)
target_compile_definitions({{NAME_ID}} PRIVATE FW_NAME="{{NAME}}" FW_VERSION="${FW_VERSION}")
target_link_libraries({{NAME_ID}} {{LIBS}})
frank_firmware({{NAME_ID}} NAME {{NAME}})
