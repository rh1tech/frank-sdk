# frank-sdk example: diag — every driver on the board, one console command each.
cmake_minimum_required(VERSION 3.19)

set(FRANK_BOARD "M2" CACHE STRING "Board")
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/board.cmake)
include(${FRANK_SDK_DIR}/pico_sdk_import.cmake)

project(diag C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_subdirectory(${FRANK_SDK_DIR} frank-sdk)

add_executable(diag main.c)
target_link_libraries(diag frank_audio)
if(FRANK_BOARD_SD_MODE)
    target_link_libraries(diag frank_storage)
endif()
if(FRANK_BOARD_VIDEO_BACKEND)
    target_link_libraries(diag frank_video)
endif()
if(TARGET frank_input)
    target_link_libraries(diag frank_input)
endif()
frank_firmware(diag)
