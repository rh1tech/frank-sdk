# Building

A firmware picks its board, includes frank-sdk, links the libraries it uses:

```cmake
set(FRANK_BOARD M2 CACHE STRING "Board")
include(frank-sdk/cmake/board.cmake)          # before the SDK
include(frank-sdk/pico_sdk_import.cmake)
project(myfw C CXX ASM)
pico_sdk_init()
add_subdirectory(frank-sdk)
add_executable(myfw src/main.c)
target_link_libraries(myfw frank_video frank_audio frank_storage frank_input)
frank_firmware(myfw)                          # the platform, linker script, console, image name
```

## Libraries

| Library | Header | Notes |
|---|---|---|
| `frank_platform` | `frank/platform.h` | always: clocks, PSRAM, heap, the console protocol, reboot, watchdog, safe mode, profiler |
| `frank_video` | `frank/video.h` | 320x240 8-bit rows or a frame buffer, a 256-entry palette; HDMI sound ring |
| `frank_audio` | `frank/audio.h` | `frank_audio_init(FRANK_AUDIO_AUTO / I2S / PWM / HDMI, rate)`, `frank_audio_write()` |
| `frank_storage` | `frank/storage.h` | `frank_sd_mount()`, then FatFs (`f_open`...) |
| `frank_input` | `frank/input.h` | `frank_input_poll()` each pass; keys as HID usages, mouse, `FRANK_PAD_*` |
| `frank_link` | `frank/link.h` | Next / Core 2 only: `frank_link_init()`, `frank_link_hello()`, `frank_link_serve()` |

## Build options

| Option | Values | Default |
|---|---|---|
| `FRANK_BOARD` | M1 M2 NX C2 SL FZ FN Z0 PC TW AF, or GA GB XT (run time) | (required) |
| `FRANK_CONSOLE` | `usb` (CDC on the native port), `uart`, `none` | the board's |
| `FRANK_RUNTIME_BOARD` | `ON`, `OFF` | `ON` for GA, GB, XT |
| `FRANK_USB_HOST` | `native`, `pio`, `none` | PIO-USB where the board has it and the console is USB; the native port with a UART console; else none |
| `FRANK_VIDEO_OUT` | `hdmi`, `vga`, `composite` | `hdmi` |
| `FRANK_HDMI_BACKEND` | `hstx`, `pio` | the board's (PIO is the only one off GPIO12-19) |
| `FRANK_CPU_MHZ`, `FRANK_PSRAM_MHZ`, `FRANK_FLASH_MHZ`, `FRANK_*_RXDELAY` | | the board file's (tuned values) |

The native USB port cannot be the console and the host at once: the build refuses
`FRANK_CONSOLE=usb` with `FRANK_USB_HOST=native`. With the host on PIO-USB the USB console
is frank-dos's dual-role one (TinyUSB device + host).

## Boards found at run time

frank-test's way: one image per package that identifies the board itself (its pin
signatures) from the fleet table (`frank_fleet`, `board_desc.h`). Build for `GA`
(RP2350A), `GB` (RP2350B), `XT` (xt8086: no console), or any target with
`-DFRANK_RUNTIME_BOARD=ON` to keep its console pins (the Next's GP46/45). The boot then
does nothing board-specific: no clock raised, no PSRAM chip select driven, no watchdog
unless `FRANK_RUNTIME_BOOT_GUARD`. Once the board is known:

```c
frank_clocks_apply(mhz, vreg, flash_mhz, -1);            // the clock it chose
size_t n = frank_psram_init_cs(d->pins.psram_cs, 133);   // the descriptor's CS
frank_heap_add_psram(n);                                 // malloc grows into it
```

`examples/runtime` does it from the console (`~C board next~`).

## CMake helpers

| Function | What it does |
|---|---|
| `frank_firmware(t [NAME n])` | the platform, the console, the linker script, panic captured, `<board>_<n>.uf2` |
| `frank_linker_script(t STACK_KB n)` | the same linker script with an n KB main stack (4..64, default 32): SRAM back for a firmware that measured its stack. After `frank_firmware()` |
| `frank_console_wrap_getchar(t)` | every `getchar_timeout_us()` returns only the bytes that are not the protocol; `~BOOT~`, `~RST~` and `~C cmd~` are acted on wherever keys are read. For firmware that reads the console as keys instead of calling `frank_console_poll()` |
| `frank_no_short_enums(t sources...)` | `-fno-short-enums` for those sources (struct layouts read from data files) |

A firmware with its own boot (frank-test) can link `frank_platform` alone and set what
`frank_firmware()` would: `FRANK_CONSOLE_USB`, `FRANK_RUNTIME_BOARD`, the linker script and
`-Wl,--wrap=panic`.

## Boards

Pins live in `boards/*.yaml`; `tools/gen_boards.py` checks them and writes
`boards/gen/` and `docs/boards/` (`--check` in CI). A new board: a YAML file with its
pins from the KiCad netlist (`kicad-cli sch export netlist`), then the generator.

## The desktop build

```
cmake -S frank-sdk/host -B build-host -DAPP_DIR=$PWD && cmake --build build-host
build-host/frank_app -sd ./sd
```

The firmware's `main()` runs on SDL2: the window is the screen, the terminal the console
(`~C help~`), a directory the SD card, the keyboard and mouse the input (SDL scancodes are
HID usages). Headless: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`.

## Tests

```
python3 tools/gen_boards.py --check
mcp/.venv/bin/python -m pytest -q mcp/tests
```
