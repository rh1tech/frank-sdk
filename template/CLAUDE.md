# {{NAME}}

{{DESCRIPTION}} — firmware for the FRANK RP2350 boards ({{BOARDS}}), built on frank-sdk
(the `frank-sdk/` submodule: board files, platform, drivers, docs, MCP servers).

## How to work here
- Pins, clocks and drivers come from frank-sdk. Never hard-code a GPIO: ask the `frank-docs`
  MCP (`board`, `pin`) or read `frank-sdk/docs/boards/<board>.md`. A board change goes into
  `frank-sdk/boards/*.yaml`, then `tools/gen_boards.py`, never into a copied header.
- Before debugging video, PSRAM, flash timing, reboots or hangs: `gotchas` (frank-docs).
- Build with `./build.sh BOARD` or the `frank-dev` MCP `build`; lint with `check` on the .elf.
- Flash only with `./flash.sh BOARD` or `frank-dev` `flash` (~BOOT~ over the USB CDC console,
  picotool by serial). Never ask for the BOOTSEL button. After every flash: read the console
  (`command BOARD bootinfo`) and look at the picture (`watch`): a frozen or black frame is a bug.
- Keep the recovery path: main() calls `frank_init()`, then `frank_boot_ok()`; the main loop
  feeds the watchdog and ticks the crumb. Reboot only through `frank_reboot*()`.
- The console protocol: `~BOOT~`, `~RST~`, `~C name args~` (`~C help~` lists the commands).
- Commits: no AI attribution anywhere (`tools/install-hooks.sh` enforces it).
