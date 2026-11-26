# Moving frank-test onto frank-sdk — the plan

Status: **phase 1 done (2026-11-26)**, in frank-sdk; frank-test unchanged so far. Next: phase 2.

## Why frank-test is different

Every other firmware picks one board when it is built. frank-test is one image per
*package* that works out at run time which of 15 boards it is on (pin signatures,
`core/detect.c`, `core/pinsig.c`) and gates each test on what the board declares:

| Image | `PICO_BOARD` | Boards it serves |
|---|---|---|
| rp2350a | `frank_a` | FRANK (Pico 2), miniFRANK, microFRANK, zeroFRANK, Core 2 / Next slave |
| rp2350b | `frank_b` | FRANK (Pico Plus 2), FRANK PGA, MegaFRANK, OldSkoolFRANK, Core 2 / 2U, Z0pa, FRANK ZX |
| next | `frank_next` | FRANK Next master (console on GP46/45) |
| xt8086 | `frank_xt8086` | xt8086 (no console UART) |
| z0 | `frank_z0` | FRANK Z0 (UART1 on GP8/9, HID on PIO-USB) |
| slave | `frank_core2_slave` | the link peer |

Its board table (`core/board_table.c`, `frank_board_desc_t`) is netlist-derived and
the best pin source in the fleet. It covers boards frank-sdk does not yet know (FRANK,
PGA, MegaFRANK, microFRANK, miniFRANK, zeroFRANK, OldSkoolFRANK, Core 2U, Z0pa,
xt8086, FRANK ZX). Its tests already take pins at run time
(`d->board->pins.*`), and its drivers are its own copies (the frank-msx / pico_hdmi /
murmnes lineage: `drivers/`, `common/link_*`).

So the move is not "replace the board header". It is:

1. **One pin source for both.** The table moves into frank-sdk's YAML, which then
   *generates* frank-test's run-time table, so frank-sdk's build-time targets and
   frank-test's run-time descriptors can never disagree again.
2. **A run-time board mode in frank-sdk** for images that find their board themselves.
3. **frank-sdk's platform under frank-test** (clocks, PSRAM, recovery, console protocol,
   linker script, bench tooling), keeping frank-test's tests, UI and detection.
4. Later, separately: **drivers with run-time pins**, so frank-test drops `drivers/`.

## Phase 0: prerequisites (yours)

- Commit the work in progress in frank-test: done, 239a330 (FRANK ZX, FRANK Z0, stereo xt8086).
- Then the work happens on a branch `frank-sdk` in a git worktree
  (`frank-test-sdk/` beside your checkout), which is left alone. Nothing is merged or
  pushed without you.

## Phase 1: the fleet in frank-sdk's YAML (frank-sdk only)

- `boards/fleet/<slug>.yaml`: one per frank-test descriptor, carrying everything
  `frank_board_desc_t` has. That means id, name, MCU class, role, caps, the full
  `frank_pins_t` (`PIN_NC` = absent), the pin signature, `pico_socket`,
  `module_memory`, `video_default`, `i2s_lr_swapped`, flash/PSRAM sizes and the manual
  note. The schema follows `core/board_desc.h` field for field. A one-off converter
  reads `board_table.c` (through a host build that dumps the table as JSON, so the C
  macros like `PINS_SD_SPI` resolve exactly as the compiler sees them).
- Each fleet board names the build target it runs as (`target: M2`, `NX`, `FZ` ...,
  or none). `gen_boards.py` then **checks every fleet board against its target**:
  same SD, HDMI, I2S, PSRAM, console pins. Any difference is a drift error. FRANK vs
  M2, Next vs NX and FRANK Z0 vs FZ get cross-checked from the first run.
- The generator writes `boards/gen/frank_fleet.c` + `frank/fleet.h`. That is the
  `frank_board_desc_t` table, with the struct moving into frank-sdk unchanged so
  frank-test's code compiles against it as it is.
- **Acceptance:** a host program built twice, once with frank-test's `board_table.c`
  and once with the generated table, dumps both as JSON. The dumps must be identical
  (every pin, cap, signature entry and string).

### Phase 1: what was done

- `boards/fleet/*.yaml`: frank-test's 14 descriptors and the fallback, imported by
  `tools/fleet/import_fleet.py` from a host build of `core/board_table.c` (frank-test
  239a330), with every comment of that file kept as `notes` / `inline_notes` /
  `signature_notes`, in table order (`order`).
- `fleet/include/`: frank-test's `board_desc.h`, `frank_caps.h`, `video_mode.h`, unchanged
  but for the licence line; `fleet/fleet.c`: its lookups, unchanged.
- `tools/gen_boards.py` (through `tools/fleet/gen_fleet.py`) writes `boards/gen/frank_fleet.c`
  (5.4 KB of flash, `-Wall -Wextra` clean), `boards/gen/fleet.json` (the MCP's `fleet` tool)
  and `docs/fleet.md`, and cross-checks every fleet board against its `target`.
- **Acceptance:** `tools/fleet/compare_fleet.py` dumps frank-test's table and the generated
  one: 15 descriptors, identical but for the Z0pa changes below. The C round trip and the
  drift check are in the tests (`mcp/tests`).

The cross-check found on its first run, and what became of it:

| Board | Found | Now |
|---|---|---|
| FRANK, PGA, miniFRANK | `pwm_l/r` NC here, 11/10 in M2 | the same wiring, two conventions: frank-test records PWM pins only where a board has no I2S; listed in `target_differences` |
| FRANK, PGA | `psram_cs` NC here, 47 in M2 | the PSRAM is on the module (`module_memory`); listed |
| MegaFRANK | `psram_cs` 31 (bit-banged SPI on GP31-34) | **not an M2 board** for PSRAM: an M2 firmware would look on GP47. `target` cleared until it has a target of its own |
| Z0pa (Waveshare PiZero) | PS/2, NES pad and PWM pins disagreed with frank-sdk's Z0 (murmnes / SpeccyP vs frank-dos / frank-quake2) | **the user's decision: frank-dos's wiring.** Z0pa now has PS/2 keyboard 2/3, mouse 0/1, NES clk 4 / latch 7 / data 6, PWM 10/11 |
| Z0pa | no PIO-USB declared | the board's own PIO-USB socket added: D+ GP28 (`CAP_PIO_USB`), already the GP28 HIGH of its signature |

frank-test's table now differs from its own repository only in Z0pa: phase 3 takes the
generated table, so frank-test's PiZero tests move to the new pins then.

Not in the fleet yet: M1 (M1P2), PC (Olimex PICO-PC) and FN (FRANK Nano), which frank-test
does not know. They go in when frank-test gets descriptors (and signatures) for them.

## Phase 2: a run-time board mode in frank-sdk

- `boards/generic_a.yaml`, `generic_b.yaml`: the package and nothing else, plus the
  console placements frank-test uses (`next`, `xt8086` and `z0` keep their own
  targets). They set `FRANK_RUNTIME_BOARD 1`.
- With `FRANK_RUNTIME_BOARD`, frank-sdk's constructor-101 init brings up only the
  clocks. **PSRAM is not touched before detection.** The chip select differs per board
  (47, 8, 0, or MegaFRANK's bit-banged SPI), and driving the wrong pin would disturb
  the detector's passive pin read. The new `frank_psram_init_cs(cs)` is called by
  frank-test once it knows the board. The heap starts SRAM-only and grows into PSRAM then.
- The console protocol, watchdog, crumbs, stall watcher, safe mode and reboot all work
  unchanged. They need no pins. The console UART is chosen at run time, as frank-test
  does now; the U key and the stored setting stay frank-test's.

## Phase 3: frank-test on frank-sdk's platform (frank-test branch)

| frank-test today | Becomes |
|---|---|
| `boards/frank_*.h` (Pico SDK headers) | frank-sdk's generated `frank_generic_a/b`, `frank_nx`, `frank_fz`, `frank_sl` and an xt8086 target |
| `core/board_table.c` | generated `frank_fleet.c` (the file is deleted; detection code unchanged) |
| `drivers/psram_init.c` | `frank/psram.h` (`frank_psram_init_cs`, `frank_psram_retime`) |
| `core/reboot.c` | `frank_reboot()` / `frank_reboot_clean()` (core 1 off, PSRAM out of QPI, clocks back) |
| its clock bring-up | `frank_clocks_init()`; the overclock campaign (`core/oc_*`) keeps its sweeps and calls `frank_flash_set_timing()` / `frank_psram_retime()` |
| its console handling of `~BOOT~` etc. | `frank_console_poll()` + `frank_console_add()` for its commands |
| (none) | watchdog, crumbs, stall watcher, safe mode → BOOTSEL, boot guard |
| linker script | `ld/memmap_frank.ld` |
| `flash.sh`, `swd_flash.sh`, `tools/` bench scripts | `frank flash` / the frank-dev MCP; frank-test's bench scripts that remain (report capture) stay |
| `.githooks` / attribution check (if absent) | frank-sdk's |
| `frank-sdk` | a submodule, pinned to a tag (`v0.2.0` once phase 1-2 are released) |

Unchanged in this step: `tests/`, `ui/`, `app/` dialogs, `core/detect.c`, `pinsig.c`,
`attest.c`, settings, and all of `drivers/` and `common/link_*`.

## Phase 4 (later, its own review): drivers

frank-sdk's drivers get run-time pin entry points next to their board-macro ones, then
frank-test switches one subsystem at a time, each verified on the bench:

| Subsystem | frank-sdk today | Needed for frank-test |
|---|---|---|
| PS/2, NES pad, I2S, TLV320 | pins already passed at init | none |
| SD (SPI) | pins from macros | `frank_sd_init_pins()` |
| HDMI PIO, composite, VGA | pins from macros | base pin and lane order at init |
| HDMI HSTX | GPIO12-19 fixed | none (all HSTX boards share it) |
| link | board macros | pins at init (frank-test's `common/link_*` is the same lineage) |
| PCM5122 hat, SPI PSRAM (MegaFRANK), TurboSound, key matrix, ESP | not in frank-sdk | move them in, or keep in frank-test |

## Verification

1. **Phase 1:** the identical-dump check above; `gen_boards.py --check` with the
   cross-checks; frank-sdk CI.
2. **Phase 3, every image:** builds with no new warnings; `frank check` lint clean.
3. **The real test, on each board on the bench:** a frank-test report (`report.c`,
   saved to SD) before the move and after, on the same board. Same board identified,
   same confidence, same tests run, same results; clock/PSRAM figures within noise.
   First on FRANK Next (on the bench now), then on whatever boards you connect.
4. The recovery path on frank-test: a forced hang ends in BOOTSEL with nobody at the
   board, and 20 warm reboots keep PSRAM.

## Risks

- **Detection before init.** frank-test reads passive pin states to identify the
  board. Anything frank-sdk drives at boot (PSRAM CS, a UART) could change what it
  sees. Phase 2 drives nothing board-specific before detection, and the dump/report
  comparisons catch a change.
- **The overclock campaign** sets flash/PSRAM timing itself; frank-sdk's
  per-board RXDELAY overrides must not fight it. In run-time mode they apply only
  to the boot defaults.
- **The slave image** stays as it is until phase 4 (it shares the link code).
