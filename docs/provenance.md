# Provenance

Where each frank-sdk file came from: which copy in which repo was the base,
and which fixes from the other copies were merged in. The variant tables are
in [inventory.md](inventory.md) (`tools/inventory.py`).

## Boards

| frank-sdk | Source of the pins | Notes |
|---|---|---|
| `boards/m2.yaml` | frank-test `core/board_table.c` PINS_* conventions (netlist); frank-dos `BOARD_M2` | clocks: frank-quake2 diag tuning (PSRAM 168 MHz RXDELAY 6, flash 126 MHz RXDELAY 5) |
| `boards/nx.yaml` | frank-test `FRANK_BOARD_NEXT`; frank-dos `BOARD_NX`, `boards/frank_next_master.h` | PIO plan from frank-dos (SDIO PIO0, HDMI+I2S PIO1, link PIO2 base 16) |
| `boards/c2.yaml` | frank-test `FRANK_BOARD_CORE2`; frank-dos `BOARD_C2` | flash /5 RXDELAY 3: frank-blood (the derived 6 bricks a soft reset) |
| `boards/sl.yaml` | frank-test "Core 2 / FRANK Next (slave)"; frank-react `drivers/link/link_pins.h` | one file for three pin-identical slaves |
| `boards/fz.yaml` | frank-test `FRANK_BOARD_Z0`, `boards/frank_z0.h` | FRANK Z0. Clocks untuned (378 MHz) |
| `boards/z0.yaml` | frank-quake2 (`BOARD_VARIANT=Z0`), frank-dos `BOARD_Z2`, `drivers/hdmi/hdmi.h` lane order | Waveshare RP2350-PiZero |

| `boards/fleet/*.yaml` | frank-test `core/board_table.c` at 239a330 (netlist-derived), imported with its comments | Z0pa's accessory pins to frank-dos's wiring and its PIO-USB socket (the user's decision, 2026-11-26) |
| `fleet/include/*.h`, `fleet/fleet.c` | frank-test `core/board_desc.h`, `frank_caps.h`, `video_mode.h`, the lookups of `board_table.c` | unchanged |

## Platform

| frank-sdk | Base | Merged in |
|---|---|---|
| `platform/psram.c` | frank-dos `drivers/psram/psram_init.c` (interrupts off in direct mode, ID read, size detection, fall-back to 84/66/33 MHz with a cap) | per-board `FRANK_PSRAM_RXDELAY` (frank-war2/quake2/blood); `FRANK_PSRAM_COOLDOWN` (frank-diablo used 3, the other nine repos 1; default 1); reset to SPI mode before the QPI enable (frank-shadowwarrior); RXDELAY clamped to its 3 bits |
| `platform/clocks.c` | frank-duke3d `common/platform/clocks.c` (XIP-cache flush and the voltage back to 1.10 V as the first runtime init, the safe flash timing before the SDK's 150 MHz, `/5` RXDELAY 3 across the PLL switch, clocks back to the boot state before a restart) | frank-dos's flash RXDELAY rule (the overflow past divider 7 fixed by scaling boot2's delay, xrip's rule) and the Quad Enable fix-up; clk_peri at about 126 MHz from any clk_sys |
| `platform/flash_config.[ch]`, `flash_timing.h` | xrip/pico-386 `drivers/flash-config` (799df1d) through frank-dos, MIT | unchanged |
| `platform/recovery.c` | frank-ecat `platform/platform.c` (reboot with core 1 forced off and PSRAM to SPI, crash capture, crumbs, stall watcher, profiler, safe mode → BOOTSEL) | frank-diablo's boot guard (3 failed boots → BOOTSEL) and the "reboot asked for from" record; frank-duke3d's `frank_clocks_for_reboot` in the reboot path; stall thresholds 1 s / ring from 0.5 s (a 2 s firmware watchdog fired before the 2 s threshold, so no PC was recorded: found on the Next 2026-11-26) |
| `platform/console.c` | frank-ecat `platform_uart_cmd` (`~BOOT~ ~RST~ ~VERB~ ~PROF~ ~C~`), frank-diablo `platform_console_poll` | new: a command table with `help`, and `OK <name>` after each command for scripts |
| `platform/heap.c` | frank-ecat `platform/ecat_malloc.c` (two dlmalloc mspaces, the spinlock with interrupts on, the C++ exception kept out of PSRAM) | board-agnostic PSRAM size; `psram_heap.c`'s game-specific tile cache left out |
| `platform/dlmalloc.c` | identical in all 8 repos (Doug Lea 2.8.6, public domain) | `dlmalloc_frank.c` sets the options |
| `platform/init.c` | frank-diablo `platform_early_init` (constructor 101: PSRAM before any C++ constructor) | core-1 guard (frank-blood: SDK 2.2 re-runs `.preinit_array` on core 1) |
| `ld/memmap_frank.ld` | pico-sdk 2.2.0 `memmap_default.ld` | frank-diablo/quake2: PSRAM sections, the 32 KB main stack (frank-blood), `mem*` from libc.a and libg.a and the 64-bit divisions in RAM |

## Drivers

| frank-sdk | Base | Merged in |
|---|---|---|
| `drivers/video/hstx/` | frank-react `drivers/hdmi` (pico_hdmi's psx_video, public domain: 640x480 and 800x600 switchable at run time, resync and FIFO counters) | frank-diablo's `psxv_beam_line()`; pins checked against the board file at compile time |
| `drivers/video/pio/` | frank-quake2 `drivers/hdmi_pio` (the only PIO HDMI that locks on the Waveshare PiZero, per frank-ecat's bench notes) | pins, lane order and PIO instance from the board file; its tables first in SCRATCH_X/Y (`ld/memmap_frank.ld`, as frank-quake2's script) |
| `drivers/video/frank_video.c` | new | one 320x240 8-bit mode over every output; HDMI audio as a 44.1 kHz ring (averaged 4:1 for hdmi_pio's 11025 Hz source); DVI mode on both HDMI back-ends (hdmi_pio's island slot filled with sync symbols, new) |
| `drivers/video/disphstx/` | frank-nes `drivers/disphstx` (Miroslav Nemecek's DispHSTX, PicoLibSDK; VGA over the HSTX with time dithering) | run on the calling core with an idle hook (`DispHstxVgaRunHere`); its DMA interrupt moved to DMA_IRQ_2 (it writes the shared vector table, frank_audio has DMA_IRQ_1); trimmed to the 8-bit pattern format, one strip, 640 pixels (30 KB of SRAM to 12); `clk_sys` stays the firmware's, `clk_hstx` 252 or 126 MHz from it. Replaces frank-test's pico_vga, which missed its ~280 ns line deadline under frank-sdk and never locked |
| `drivers/video/vga_pio/` | quakegeneric `drivers/vga-nextgen` (the Murmulator's PIO VGA, GPL-2.0) | rewritten for frank/video.h: whole 800-pixel lines with their syncs, a control DMA channel, a line of slack per interrupt; any pin base (M1's GPIO6) |
| `drivers/video/composite/` | frank-test `drivers/tv` (murmnes / frank-msx tv-software, PAL/NTSC over PIO) | built as frank-test builds it (`composite_tv.c`); fixed `sm_config_set_clkdiv(PIO->sm, ...)`, which wrote SM0's CLKDIV register whatever the video's state machine; rows from a callback, a vsync per field, PAL/NTSC switch, its symbols renamed `tv_*` (`tv_rename.h`); 504 MHz only |
| `drivers/storage/sd_spi/` | frank-diablo `drivers/sdcard` (frank-castle's block CRC-16 check, retries, card re-init; configurable clock) | pins from the board; hardware SPI when the pins fit an SPI block, PIO SPI otherwise; the SPI fall-back for SDIO renamed by a wrapper (frank-dos's compile definitions do not reach interface sources) |
| `drivers/storage/sdio/` | frank-dos `drivers/sdio` (SDIO_RP2350, Rabbit Hole Computing, MIT; frank-react's card layer with bounce buffer and cache) | PIO and pins from the board |
| `drivers/storage/fatfs/` | FatFs R0.15 w/patch1 (8 repos; frank-dos/murm386 had R0.14b) | `ffconf.h`'s fleet-differing settings overridable |
| `drivers/audio/i2s.c` | frank-diablo / frank-quake2 ping-pong DMA I2S | PIO2's pin function |
| `drivers/audio/tlv320.c` | frank-dos | unchanged |
| `drivers/audio/frank_audio.c` | new; the I2S/PWM probe is frank-dos's `testPins` (`platform/test_pins.c`) | PWM by DMA paced by the PWM wrap, one slice or two (frank-dos drove it from a timer IRQ per sample) |
| `drivers/input/usbhid/` | frank-war2 / frank-castle (XInput, the report parser, pad layouts) | frank-dos's PIO-USB host init, `tusb_config.h` (hub, 8 HID, CDC device) and dual-role `usb_console.c` |
| `drivers/input/ps2/` | frank-dos unified PS/2 (keyboard and mouse on one PIO) | new `ps2_set2.c`: set-2 codes to HID usages with the murmulator library's tables |
| `drivers/input/nespad/` | frank-dos | PIO from the board |
| `drivers/input/pio_usb/` | Pico-PIO-USB 0.7.2 (sekigon-gonnoc, MIT) through frank-dos | unchanged |
| `drivers/link/` | frank-blood / frank-amiga `common/link` (superset of frank-react's: resync, idle check, async bulk) | generic `link_proto.h` (its game's operations taken out); `frank_link.c`: HELLO with the clock check, ECHO soak, REBOOT |
| `host/` | frank-dos `host/ff_host.c`, `host_dir.c` (FatFs on a host directory) | new SDL2 implementation of the APIs |
| `tools/psram_unstick` | frank-react's recipe (its program was lost), rebuilt | RAM-only (no_flash) |

## Verified on hardware

| Date | Board | What |
|---|---|---|
| 2026-11-26 | NX (FRANK Next master) | `examples/hello`: 504 MHz at 1.65 V, flash /8 RXDELAY 7 (`60007708`), PSRAM 8 MB ID 0D 5D; 20/20 warm `~RST~` reboots with PSRAM intact; `~C hang~` → watchdog → safe mode (stage, arg, stall PCs resolving to the hanging line) → BOOTSEL → `picotool load` with nobody at the board |
| 2026-11-26 | NX | `examples/video`: HSTX 640x480@60 locked on the MS2109 capture (bars, ramp, moving box between grabs); firmware counts 59.98 fps and exactly 44100 HDMI audio frames a second. The capture stick's pitch is off by +4..9 % and differs per frequency: the stick, not the firmware (frank-dos saw it 8 % low) |
| 2026-11-26 | NX | `examples/diag`: SDIO card FAT32 mounted, 1 MB write/read-back intact (1.5 MB/s write, 7.1 MB/s read through FatFs; 12.7 MB/s raw at 72 MHz); TLV320 configured, I2S at exactly 44100 frames a second; HDMI audio through `frank_audio` at 44100; the HDMI-audio feed hang caught by safe mode and fixed |
| 2026-11-26 | NX | `frank unstick NX`: the RAM-only program ran, back to BOOTSEL, the next flash verified |
| 2026-11-26 | NX | a firmware scaffolded with video, audio, storage and input: built, linted, flashed, card and sound checked, picture changing |
| 2026-11-26 | host | hello, video, diag on SDL2 (headless): console commands, FatFs on a directory, the audio ring |
