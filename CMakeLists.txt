# frank-sdk — libraries and frank_firmware(). add_subdirectory() it after
# pico_sdk_init(), with cmake/board.cmake included before the SDK (see there).
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: MIT

if(NOT DEFINED FRANK_SDK_DIR)
    message(FATAL_ERROR "frank-sdk: include(frank-sdk/cmake/board.cmake) before pico_sdk_import.cmake")
endif()

file(READ "${FRANK_SDK_DIR}/version.txt" FRANK_SDK_VERSION)
string(STRIP "${FRANK_SDK_VERSION}" FRANK_SDK_VERSION)

# ---- the platform: clocks, PSRAM, heap, console, reboot, hang recovery ----
add_library(frank_platform INTERFACE)
target_sources(frank_platform INTERFACE
    ${FRANK_SDK_DIR}/platform/clocks.c
    ${FRANK_SDK_DIR}/platform/console.c
    ${FRANK_SDK_DIR}/platform/flash_config.c
    ${FRANK_SDK_DIR}/platform/heap.c
    ${FRANK_SDK_DIR}/platform/init.c
    ${FRANK_SDK_DIR}/platform/psram.c
    ${FRANK_SDK_DIR}/platform/recovery.c
    ${FRANK_SDK_DIR}/platform/dlmalloc_frank.c
    ${FRANK_SDK_DIR}/platform/test_pins.c
)
target_include_directories(frank_platform INTERFACE
    ${FRANK_SDK_DIR}/platform/include
    ${FRANK_SDK_DIR}/platform
)
target_link_libraries(frank_platform INTERFACE
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_flash
    hardware_vreg
    hardware_watchdog
    hardware_xip_cache
)
target_compile_definitions(frank_platform INTERFACE
    FRANK_SDK_VERSION="${FRANK_SDK_VERSION}"
    # frank_clocks_init() replaces the SDK's clock choice
    PICO_STDIO_UART_SUPPORT_CHARS_AVAILABLE_CALLBACK=0
    # core 1's stack is in main RAM (frank_launch_core1): SCRATCH_X stays free
    # for time-critical code and the PIO HDMI palette table
    PICO_CORE1_STACK_SIZE=0
)

# ---- video: HDMI over HSTX or PIO, as the board needs (frank/video.h) ----
add_library(frank_video INTERFACE)
target_sources(frank_video INTERFACE ${FRANK_SDK_DIR}/drivers/video/frank_video.c)
target_include_directories(frank_video INTERFACE
    ${FRANK_SDK_DIR}/drivers/video/include
    ${FRANK_SDK_DIR}/drivers/video/hstx      # hstx_packet.h is shared by both
)
# the output: hdmi (default), vga or composite, one per build (shared pins)
set(FRANK_VIDEO_OUT "hdmi" CACHE STRING "Video output: hdmi, vga or composite")
set_property(CACHE FRANK_VIDEO_OUT PROPERTY STRINGS hdmi vga composite)
if(FRANK_VIDEO_OUT STREQUAL "vga")
    # hstx: Miroslav Nemecek's DispHSTX (frank-nes, frank-msx), GPIO12-19 only,
    # time-dithered colour; pio: any pin base (M1's GPIO6), the Murmulator's
    # 2-bit ladder with a 2-pixel dither. The default: hstx where the ladder
    # is on GPIO12-19.
    if(FRANK_BOARD_VGA_BASE STREQUAL "12")
        set(_vga_default hstx)
    else()
        set(_vga_default pio)
    endif()
    set(FRANK_VGA_BACKEND "${_vga_default}" CACHE STRING "VGA back-end: hstx (DispHSTX) or pio")
    set_property(CACHE FRANK_VGA_BACKEND PROPERTY STRINGS hstx pio)
    if(FRANK_VGA_BACKEND STREQUAL "hstx")
        set(_dh ${FRANK_SDK_DIR}/drivers/video/disphstx)
        # disphstx.c #includes the library's other C files (a unity build)
        target_sources(frank_video INTERFACE ${_dh}/disphstx.c ${_dh}/disphstx_vga_m33.S ${_dh}/disphstx_dvi_m33.S)
        target_include_directories(frank_video INTERFACE ${_dh})
        target_compile_definitions(frank_video INTERFACE FRANK_VIDEO_VGA_DISPHSTX=1
            USE_DISPHSTX=1 DISPHSTX_PICOSDK=1 DISPHSTX_USE_DVI=0 DISPHSTX_USE_VGA=1
            USE_DRAWCAN=0 USE_TEXT=0 USE_RAND=0
            # only what frank_video uses (the 8-row pattern, a colour
            # separator), one strip, 640 pixels: 30 KB of SRAM down to ~12
            DISPHSTX_WIDTHMAX=640 DISPHSTX_STRIP_MAX=1 DISPHSTX_SLOT_MAX=2
            DISPHSTX_USE_FORMAT_1=0
            DISPHSTX_USE_FORMAT_1_PAL=0
            DISPHSTX_USE_FORMAT_12=0
            DISPHSTX_USE_FORMAT_15=0
            DISPHSTX_USE_FORMAT_16=0
            DISPHSTX_USE_FORMAT_2=0
            DISPHSTX_USE_FORMAT_2_PAL=0
            DISPHSTX_USE_FORMAT_3=0
            DISPHSTX_USE_FORMAT_3_PAL=0
            DISPHSTX_USE_FORMAT_4=0
            DISPHSTX_USE_FORMAT_4_PAL=0
            DISPHSTX_USE_FORMAT_6=0
            DISPHSTX_USE_FORMAT_6_PAL=0
            DISPHSTX_USE_FORMAT_8=0
            DISPHSTX_USE_FORMAT_8_PAL=0
            DISPHSTX_USE_FORMAT_ATEXT=0
            DISPHSTX_USE_FORMAT_ATTR1_PAL=0
            DISPHSTX_USE_FORMAT_ATTR2_PAL=0
            DISPHSTX_USE_FORMAT_ATTR3_PAL=0
            DISPHSTX_USE_FORMAT_ATTR4_PAL=0
            DISPHSTX_USE_FORMAT_ATTR5_PAL=0
            DISPHSTX_USE_FORMAT_ATTR6_PAL=0
            DISPHSTX_USE_FORMAT_ATTR7_PAL=0
            DISPHSTX_USE_FORMAT_ATTR8_PAL=0
            DISPHSTX_USE_FORMAT_HSTX_15=0
            DISPHSTX_USE_FORMAT_HSTX_16=0
            DISPHSTX_USE_FORMAT_MTEXT=0
            DISPHSTX_USE_FORMAT_PAT_8=0
            DISPHSTX_USE_FORMAT_RLE8=0
            DISPHSTX_USE_FORMAT_RLE8_PAL=0
            DISPHSTX_USE_FORMAT_TILE16_8=0
            DISPHSTX_USE_FORMAT_TILE16_8_PAL=0
            DISPHSTX_USE_FORMAT_TILE32_8=0
            DISPHSTX_USE_FORMAT_TILE32_8_PAL=0
            DISPHSTX_USE_FORMAT_TILE4_8=0
            DISPHSTX_USE_FORMAT_TILE4_8_PAL=0
            DISPHSTX_USE_FORMAT_TILE8_8=0
            DISPHSTX_USE_FORMAT_TILE8_8_PAL=0)
        target_link_libraries(frank_video INTERFACE hardware_pll hardware_vreg)
        set_source_files_properties(${_dh}/disphstx.c PROPERTIES COMPILE_OPTIONS
            "-Wno-unused-function;-Wno-unused-variable;-Wno-unused-parameter;-Wno-unused-but-set-variable;-Wno-sign-compare;-Wno-incompatible-pointer-types;-Wno-int-conversion;-Wno-pointer-sign")
    else()
        target_sources(frank_video INTERFACE ${FRANK_SDK_DIR}/drivers/video/vga_pio/vga_pio.c)
        target_include_directories(frank_video INTERFACE ${FRANK_SDK_DIR}/drivers/video/vga_pio)
        target_compile_definitions(frank_video INTERFACE FRANK_VIDEO_VGA_PIO=1)
    endif()
    target_compile_definitions(frank_video INTERFACE FRANK_VIDEO_OUT_VGA=1)
elseif(FRANK_VIDEO_OUT STREQUAL "composite")
    target_sources(frank_video INTERFACE ${FRANK_SDK_DIR}/drivers/video/composite/composite_tv.c)
    target_include_directories(frank_video INTERFACE ${FRANK_SDK_DIR}/drivers/video/composite)
    target_compile_definitions(frank_video INTERFACE FRANK_VIDEO_OUT_COMPOSITE=1
        TV_BASE_PIN=FRANK_VIDEO_COMPOSITE_BASE "PIO_VIDEO=PIO_INSTANCE(FRANK_PIO_VIDEO)")
endif()
# the board's back-end, or PIO on an HSTX board (HSTX only reaches GPIO12-19)
set(FRANK_HDMI_BACKEND "${FRANK_BOARD_VIDEO_BACKEND}" CACHE STRING "HDMI back-end: hstx or pio")
if(FRANK_HDMI_BACKEND STREQUAL "hstx" AND NOT FRANK_BOARD_VIDEO_BACKEND STREQUAL "hstx")
    message(FATAL_ERROR "frank-sdk: ${FRANK_BOARD}'s HDMI pins are out of HSTX's reach: FRANK_HDMI_BACKEND=pio")
endif()
if(FRANK_HDMI_BACKEND STREQUAL "pio" AND FRANK_BOARD_VIDEO_BACKEND STREQUAL "hstx")
    target_compile_definitions(frank_video INTERFACE FRANK_VIDEO_USE_PIO=1)
endif()
if(NOT FRANK_VIDEO_OUT STREQUAL "hdmi")
    # no HDMI sources
elseif(FRANK_HDMI_BACKEND STREQUAL "hstx")
    target_sources(frank_video INTERFACE
        ${FRANK_SDK_DIR}/drivers/video/hstx/psx_video.c
        ${FRANK_SDK_DIR}/drivers/video/hstx/hstx_packet.c
        ${FRANK_SDK_DIR}/drivers/video/hstx/hstx_data_island_queue.c
    )
elseif(FRANK_HDMI_BACKEND STREQUAL "pio")
    target_sources(frank_video INTERFACE
        ${FRANK_SDK_DIR}/drivers/video/pio/hdmi_pio.c
        ${FRANK_SDK_DIR}/drivers/video/hstx/hstx_packet.c
    )
    target_include_directories(frank_video INTERFACE ${FRANK_SDK_DIR}/drivers/video/pio)
endif()
target_link_libraries(frank_video INTERFACE hardware_dma hardware_irq hardware_pio pico_stdlib)

# frank_firmware(<target> [NAME <release name>])
#   - the board's console (FRANK_CONSOLE: usb or uart), the frank-sdk linker
#     script, panic captured, the C++ exception kept out of PSRAM;
#   - short enums stay (newlib and the SDK are built with them). Code that
#     reads struct layouts from data files (game ports) takes
#     frank_no_short_enums(<sources>), and never shares an enum-typed struct
#     member with SDK or driver code: use uint8_t/uint16_t there (frank-castle);
#   - the image named <board>_<name>.uf2 next to the usual outputs.
function(frank_firmware target)
    cmake_parse_arguments(FW "" "NAME" "" ${ARGN})
    if(NOT FW_NAME)
        set(FW_NAME ${target})
    endif()
    target_link_libraries(${target} frank_platform)
    # with frank_input's USB host linked, the SDK's stdio_usb compiles to nothing:
    # a USB console is then frank-dos's dual-role usb_console (host on PIO-USB)
    get_target_property(_libs ${target} LINK_LIBRARIES)
    set(_host OFF)
    if("frank_input" IN_LIST _libs AND NOT FRANK_USB_HOST STREQUAL "none")
        set(_host ON)
    endif()
    if(FRANK_CONSOLE STREQUAL "usb" AND _host)
        pico_enable_stdio_usb(${target} 0)
        pico_enable_stdio_uart(${target} 0)
        target_sources(${target} PRIVATE ${FRANK_SDK_DIR}/drivers/input/usbhid/usb_console.c)
        target_compile_definitions(${target} PRIVATE FRANK_CONSOLE_USB=1 USB_CDC_CONSOLE=1)
        target_link_libraries(${target} tinyusb_device)
    elseif(FRANK_CONSOLE STREQUAL "usb")
        pico_enable_stdio_usb(${target} 1)
        pico_enable_stdio_uart(${target} 0)
        target_compile_definitions(${target} PRIVATE FRANK_CONSOLE_USB=1)
    elseif(FRANK_CONSOLE STREQUAL "uart")
        pico_enable_stdio_usb(${target} 0)
        pico_enable_stdio_uart(${target} 1)
        target_compile_definitions(${target} PRIVATE FRANK_CONSOLE_USB=0)
    elseif(FRANK_CONSOLE STREQUAL "none")
        # no console at all (xt8086: every pin a UART could use belongs to the CPU bus)
        pico_enable_stdio_usb(${target} 0)
        pico_enable_stdio_uart(${target} 0)
        target_compile_definitions(${target} PRIVATE FRANK_CONSOLE_USB=0)
    else()
        message(FATAL_ERROR "frank-sdk: FRANK_CONSOLE must be usb, uart or none")
    endif()
    if(FRANK_RUNTIME_BOARD)
        target_compile_definitions(${target} PRIVATE FRANK_RUNTIME_BOARD=1)
    else()
        target_compile_definitions(${target} PRIVATE FRANK_RUNTIME_BOARD=0)
    endif()
    pico_set_linker_script(${target} ${FRANK_SDK_DIR}/ld/memmap_frank.ld)
    target_link_options(${target} PRIVATE
        -Wl,--wrap=panic
        -Wl,--wrap=__cxa_allocate_exception
        -Wl,--print-memory-usage
    )
    string(TOLOWER "${FRANK_BOARD}" _bl)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${_bl}_${FW_NAME}")
    pico_add_extra_outputs(${target})
endfunction()

# frank_linker_script(<target> [STACK_KB <n>] [BSS_OVERLAY <objlib>...]):
# frank-sdk's linker script, changed:
#   STACK_KB n   a main stack of n KB instead of 32 (a power of two, 4..64), for
#                a firmware that needs the SRAM and has measured its stack
#                (frank-test: 8 KB, where it ran on 4);
#   BSS_OVERLAY  the .bss of these OBJECT libraries shares one SRAM region (an
#                OVERLAY): for firmware that runs one of them at a time and
#                never touches the others' data (frank-idtech1: Doom, Heretic,
#                Hexen, Strife). crt0 zeroes the whole of it at boot.
# frank_firmware() sets the plain script; call this after it.
function(frank_linker_script target)
    cmake_parse_arguments(LS "" "STACK_KB" "BSS_OVERLAY" ${ARGN})
    if(NOT LS_STACK_KB)
        set(LS_STACK_KB 32)
    endif()
    if(NOT LS_STACK_KB MATCHES "^(4|8|16|32|64)$")
        message(FATAL_ERROR "frank_linker_script: STACK_KB must be 4, 8, 16, 32 or 64")
    endif()
    file(READ ${FRANK_SDK_DIR}/ld/memmap_frank.ld _ld)
    math(EXPR _origin "0x20080000 - ${LS_STACK_KB} * 1024" OUTPUT_FORMAT HEXADECIMAL)
    string(REPLACE "LENGTH = 512k - 32k" "LENGTH = 512k - ${LS_STACK_KB}k" _ld "${_ld}")
    string(REPLACE "ORIGIN = 0x20078000, LENGTH = 32k" "ORIGIN = ${_origin}, LENGTH = ${LS_STACK_KB}k" _ld "${_ld}")
    string(REPLACE "32 KB main stack" "${LS_STACK_KB} KB main stack" _ld "${_ld}")
    string(REPLACE ". = . + 32K;" ". = . + ${LS_STACK_KB}K;" _ld "${_ld}")
    if(LS_BSS_OVERLAY)
        set(_bss_in "        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(.sram_bss*) /* frank-sdk */
        *(COMMON)
        PROVIDE(__global_pointer$ = . + 2K);
        *(.sbss*)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM
")
        string(FIND "${_ld}" "${_bss_in}" _at)
        if(_at LESS 0)
            message(FATAL_ERROR "frank_linker_script: BSS_OVERLAY no longer matches ld/memmap_frank.ld's .bss")
        endif()
        set(_ex "")
        foreach(_l ${LS_BSS_OVERLAY})
            string(APPEND _ex " *${_l}.dir*")
        endforeach()
        set(_ov "")
        foreach(_l ${LS_BSS_OVERLAY})
            string(APPEND _ov "        .bss.${_l} {
            *${_l}.dir*(.bss*)
            *${_l}.dir*(COMMON)
            *${_l}.dir*(.sbss*)
        }
")
        endforeach()
        set(_bss_out "        *(EXCLUDE_FILE(${_ex}) .bss*)
        *(.sram_bss*) /* frank-sdk */
        *(EXCLUDE_FILE(${_ex}) COMMON)
        PROVIDE(__global_pointer$ = . + 2K);
        *(EXCLUDE_FILE(${_ex}) .sbss*)
        . = ALIGN(4);
    } > RAM

    /* frank-sdk BSS_OVERLAY: one of these runs at a time, they share the RAM */
    OVERLAY : NOCROSSREFS {
${_ov}    } > RAM

    .bss_tail (NOLOAD) : {
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM
")
        string(REPLACE "${_bss_in}" "${_bss_out}" _ld "${_ld}")
    endif()
    set(_out ${CMAKE_CURRENT_BINARY_DIR}/${target}_memmap_frank.ld)
    file(WRITE ${_out} "${_ld}")
    pico_set_linker_script(${target} ${_out})
endfunction()

# frank_console_wrap_getchar(<target>): every getchar_timeout_us() in the
# target gets the console with the protocol (~BOOT~, ~RST~, ~C ...~) taken
# out and acted on, for firmware whose own code reads the console as keys.
function(frank_console_wrap_getchar target)
    target_compile_definitions(${target} PRIVATE FRANK_CONSOLE_WRAP_GETCHAR=1)
    # getchar_timeout_us() is an inline alias: the function behind it
    target_link_options(${target} PRIVATE -Wl,--wrap=stdio_getchar_timeout_us)
endfunction()

# -fno-short-enums for these sources only (see frank_firmware); the linker's
# enum-size warning is expected then and silenced for the target.
function(frank_no_short_enums target)
    set_source_files_properties(${ARGN} TARGET_DIRECTORY ${target} PROPERTIES COMPILE_OPTIONS -fno-short-enums)
    target_link_options(${target} PRIVATE -Wl,--no-enum-size-warning)
endfunction()

# ---- storage: the microSD (SPI or SDIO, as the board has it) and FatFs (frank/storage.h) ----
add_library(frank_storage INTERFACE)
target_sources(frank_storage INTERFACE
    ${FRANK_SDK_DIR}/drivers/storage/frank_storage.c
    ${FRANK_SDK_DIR}/drivers/storage/fatfs/ff.c
    ${FRANK_SDK_DIR}/drivers/storage/fatfs/ffsystem.c
    ${FRANK_SDK_DIR}/drivers/storage/fatfs/ffunicode.c
    ${FRANK_SDK_DIR}/drivers/storage/sd_spi/pio_spi.c
)
target_include_directories(frank_storage INTERFACE
    ${FRANK_SDK_DIR}/drivers/storage/include
    ${FRANK_SDK_DIR}/drivers/storage/fatfs
    ${FRANK_SDK_DIR}/drivers/storage/sd_spi
)
pico_generate_pio_header(frank_storage ${FRANK_SDK_DIR}/drivers/storage/sd_spi/spi.pio)
if(FRANK_BOARD_SD_MODE STREQUAL "sdio")
    target_sources(frank_storage INTERFACE
        ${FRANK_SDK_DIR}/drivers/storage/sdio/sd_sdio.c
        ${FRANK_SDK_DIR}/drivers/storage/sdio/sdio_rp2350.cpp
        ${FRANK_SDK_DIR}/drivers/storage/sd_spi/sd_spi_fallback.c
    )
    target_include_directories(frank_storage INTERFACE ${FRANK_SDK_DIR}/drivers/storage/sdio)
    target_compile_definitions(frank_storage INTERFACE SD_SPI_FALLBACK=1)
    pico_generate_pio_header(frank_storage ${FRANK_SDK_DIR}/drivers/storage/sdio/sdio_rp2350.pio)
elseif(FRANK_BOARD_SD_MODE STREQUAL "spi")
    target_sources(frank_storage INTERFACE ${FRANK_SDK_DIR}/drivers/storage/sd_spi/sdcard.c)
endif()
target_link_libraries(frank_storage INTERFACE frank_platform hardware_spi hardware_pio hardware_dma hardware_gpio)

# ---- audio: I2S (+ TLV320), PWM or HDMI (frank/audio.h) ----
add_library(frank_audio INTERFACE)
target_sources(frank_audio INTERFACE
    ${FRANK_SDK_DIR}/drivers/audio/frank_audio.c
    ${FRANK_SDK_DIR}/drivers/audio/i2s.c
    ${FRANK_SDK_DIR}/drivers/audio/tlv320.c
)
target_include_directories(frank_audio INTERFACE ${FRANK_SDK_DIR}/drivers/audio/include ${FRANK_SDK_DIR}/drivers/audio)
pico_generate_pio_header(frank_audio ${FRANK_SDK_DIR}/drivers/audio/audio_i2s.pio)
target_link_libraries(frank_audio INTERFACE frank_platform hardware_pio hardware_dma hardware_pwm hardware_i2c hardware_irq)

# ---- the wall clock: DS3231 or RV-3032, on the board's I2C or the caller's bus (frank/rtc.h) ----
add_library(frank_rtc INTERFACE)
target_sources(frank_rtc INTERFACE ${FRANK_SDK_DIR}/drivers/rtc/rtc.c)
target_include_directories(frank_rtc INTERFACE ${FRANK_SDK_DIR}/drivers/rtc/include)
target_link_libraries(frank_rtc INTERFACE pico_stdlib hardware_i2c)

# ---- input: USB host (native or PIO-USB), PS/2, NES pads (frank/input.h) ----
add_library(frank_input INTERFACE)
target_sources(frank_input INTERFACE ${FRANK_SDK_DIR}/drivers/input/frank_input.c)
target_include_directories(frank_input INTERFACE ${FRANK_SDK_DIR}/drivers/input/include)
target_link_libraries(frank_input INTERFACE frank_platform hardware_pio hardware_clocks)
if(FRANK_BOARD_HAS_PS2)
    target_sources(frank_input INTERFACE
        ${FRANK_SDK_DIR}/drivers/input/ps2/ps2.c
        ${FRANK_SDK_DIR}/drivers/input/ps2/ps2_set2.c)
    # ps2_set2.c names the keys by TinyUSB's HID_KEY_*: its header, USB host or not
    target_include_directories(frank_input INTERFACE ${FRANK_SDK_DIR}/drivers/input/ps2 ${PICO_TINYUSB_PATH}/src)
    pico_generate_pio_header(frank_input ${FRANK_SDK_DIR}/drivers/input/ps2/ps2.pio)
endif()
if(FRANK_BOARD_HAS_NESPAD)
    target_sources(frank_input INTERFACE ${FRANK_SDK_DIR}/drivers/input/nespad/nespad.c)
    target_include_directories(frank_input INTERFACE ${FRANK_SDK_DIR}/drivers/input/nespad)
endif()
set(_hid ${FRANK_SDK_DIR}/drivers/input/usbhid)
if(FRANK_USB_HOST STREQUAL "native" OR FRANK_USB_HOST STREQUAL "pio")
    target_sources(frank_input INTERFACE
        ${_hid}/hid_app.c ${_hid}/hid_pad_layout.c ${_hid}/hid_ri.c ${_hid}/hid_rip.c ${_hid}/xinput_host.c)
    target_include_directories(frank_input INTERFACE ${_hid})
    target_link_libraries(frank_input INTERFACE tinyusb_host tinyusb_board hardware_dma)
    if(FRANK_USB_HOST STREQUAL "pio")
        target_compile_definitions(frank_input INTERFACE FRANK_USB_HOST=2 CFG_TUH_RPI_PIO_USB=1
            PIO_USB_DP_PIN=${FRANK_BOARD_PIO_USB_DP} PIO_USB_PIO=${FRANK_BOARD_PIO_PIO_USB})
        target_link_libraries(frank_input INTERFACE tinyusb_pico_pio_usb)
    else()
        target_compile_definitions(frank_input INTERFACE FRANK_USB_HOST=1)
    endif()
else()
    target_compile_definitions(frank_input INTERFACE FRANK_USB_HOST=0)
endif()

# ---- the master-slave link (FRANK Next, Core 2): frank/link.h ----
if(FRANK_BOARD_PAIR OR FRANK_BOARD_ROLE STREQUAL "slave")
    add_library(frank_link INTERFACE)
    target_sources(frank_link INTERFACE
        ${FRANK_SDK_DIR}/drivers/link/frank_link.c
        ${FRANK_SDK_DIR}/drivers/link/link_bus.c
        ${FRANK_SDK_DIR}/drivers/link/link_proto.c
        ${FRANK_SDK_DIR}/drivers/link/link_session.c)
    target_include_directories(frank_link INTERFACE ${FRANK_SDK_DIR}/drivers/link/include ${FRANK_SDK_DIR}/drivers/link)
    pico_generate_pio_header(frank_link ${FRANK_SDK_DIR}/drivers/link/link_bus.pio)
    target_link_libraries(frank_link INTERFACE frank_platform hardware_dma hardware_pio hardware_clocks)
endif()

# ---- the fleet: frank-test's run-time board table (board_desc.h), generated from
#      boards/fleet/*.yaml, for firmware that finds its board at run time ----
add_library(frank_fleet INTERFACE)
target_sources(frank_fleet INTERFACE ${FRANK_SDK_DIR}/boards/gen/frank_fleet.c ${FRANK_SDK_DIR}/fleet/fleet.c)
target_include_directories(frank_fleet INTERFACE ${FRANK_SDK_DIR}/fleet/include)
