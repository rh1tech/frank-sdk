# frank-sdk host build: a firmware's sources on SDL2, on the desktop.
#   cmake -S frank-sdk/host -B build-host -DAPP_DIR=path/to/firmware [-DAPP_SOURCES="src/a.c;src/b.c"]
#   build-host/frank_app [-sd DIR] [-scale N]
# APP_SOURCES defaults to APP_DIR/main.c or APP_DIR/src/main.c.
cmake_minimum_required(VERSION 3.19)
project(frank_host C CXX)
set(CMAKE_C_STANDARD 11)
get_filename_component(FRANK_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT APP_DIR)
    message(FATAL_ERROR "frank-sdk host: -DAPP_DIR=<the firmware's directory>")
endif()
if(NOT APP_SOURCES)
    if(EXISTS ${APP_DIR}/src/main.c)
        set(APP_SOURCES src/main.c)
    else()
        set(APP_SOURCES main.c)
    endif()
endif()
list(TRANSFORM APP_SOURCES PREPEND "${APP_DIR}/")
find_package(SDL2 REQUIRED)
file(READ "${FRANK_SDK_DIR}/version.txt" FRANK_SDK_VERSION)
string(STRIP "${FRANK_SDK_VERSION}" FRANK_SDK_VERSION)

add_executable(frank_app
    ${APP_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/frank_host.c
    ${CMAKE_CURRENT_LIST_DIR}/ff_host.c
    ${CMAKE_CURRENT_LIST_DIR}/host_dir.c
    ${FRANK_SDK_DIR}/platform/console.c
    ${FRANK_SDK_DIR}/drivers/rtc/rtc.c)   # no I2C here: frank_rtc_init_board() finds none
set_source_files_properties(${APP_SOURCES} PROPERTIES COMPILE_DEFINITIONS "main=frank_app_main")
target_include_directories(frank_app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${FRANK_SDK_DIR}/platform/include
    ${FRANK_SDK_DIR}/drivers/video/include
    ${FRANK_SDK_DIR}/drivers/audio/include
    ${FRANK_SDK_DIR}/drivers/input/include
    ${FRANK_SDK_DIR}/drivers/rtc/include
    ${FRANK_SDK_DIR}/drivers/storage/include
    ${FRANK_SDK_DIR}/drivers/storage/fatfs)
target_compile_definitions(frank_app PRIVATE FRANK_SDK_VERSION="${FRANK_SDK_VERSION}"
    FW_NAME="host" FW_VERSION="${FRANK_SDK_VERSION}")
target_link_libraries(frank_app PRIVATE SDL2::SDL2 pthread)
