# frank-sdk

The common base of the FRANK RP2350 firmware: board definitions, the platform (clocks,
PSRAM, heap, console, reboot and hang recovery), drivers, examples, the fleet's
hard-won lessons, and MCP servers that build, flash and check firmware on the bench.

| Target | Board | Video | Sound | Input |
|---|---|---|---|---|
| `M2` | FRANK M2 / Murmulator 2.0 pinout (FRANK, PGA, MegaFRANK, miniFRANK) | HDMI (HSTX or PIO), VGA, composite | I2S, PWM, HDMI | USB, PS/2, NES |
| `M1` | Murmulator 1.x + Pico 2 ("M1P2") | HDMI (PIO) | I2S, PWM, HDMI | USB, PS/2, NES |
| `NX` + `SL` | FRANK Next, master + slave | HDMI (HSTX) | TLV320 (I2S), HDMI | USB (hub) |
| `C2` + `SL` | FRANK Core 2 / 2U, master + slave (experimental: firmware need not support it) | HDMI (HSTX) | I2S, HDMI | USB |
| `FZ` | FRANK Z0 | HDMI (HSTX) | TLV320 (I2S), PCM5122 DAC HAT, HDMI | PIO-USB |
| `FN` | FRANK Nano | HDMI (HSTX) | HDMI | PIO-USB |
| `Z0` | Waveshare RP2350-PiZero | HDMI (PIO) | PCM5122 DAC HAT, I2S module, PWM, HDMI | USB / PIO-USB (PS/2 when not PIO-USB), NES |
| `PC` | Olimex PICO-PC + Pico 2 | HDMI (HSTX), VGA | PWM, HDMI | USB, PS/2, NES |
| `TW` | UKTailwind Pico Computer 3 | HDMI (HSTX) | PCM5102A (I2S), HDMI | USB (hub); PS/2, NES on its I/O header |
| `OL` | Olimex RP2350pc (its own RP2350B; not the PICO-PC carrier) | HDMI (HSTX, its own pair order) | ES8311 (I2S), HDMI | USB (hub) |
| `AF` | Adafruit Fruit Jam (not yet run here) | HDMI (HSTX) | TLV320 (I2S), HDMI | PIO-USB |
| `GA`, `GB`, `XT` | any board of the package, found at run time (frank-test's images; XT = xt8086, no console) | the firmware's | the firmware's | the firmware's |
| `HOST` | the desktop (SDL2): `cmake -S host -DAPP_DIR=...` | window | SDL | keyboard, mouse |

Every HDMI output carries sound (data islands) or runs as DVI (`frank_video_set_dvi`); a
board with HSTX pins can take the PIO back-end too (`-DFRANK_HDMI_BACKEND=pio`).
`boards/unverified/` holds boards whose sources contradict themselves (Pimoroni Pico DV).

`boards/fleet/` is the run-time side: every board frank-test can identify (14 and a
fallback), its full pin map, capabilities and pin signature, generated into the C table
`frank_fleet` and cross-checked pin for pin against the build target each runs as
([docs/fleet.md](docs/fleet.md)).

Pins live only in [`boards/*.yaml`](boards); `tools/gen_boards.py` checks them (no pin
twice, each PIO inside one GPIO window, the two halves of a pair at one clock) and
generates the board headers, [the pin tables](docs/boards) and the MCP's data.

## Use

- New firmware: [docs/new-firmware.md](docs/new-firmware.md)
- The bench: [docs/bench.md](docs/bench.md)
- Before debugging anything: [docs/gotchas](docs/gotchas/README.md)
- Libraries and build options: [docs/building.md](docs/building.md)
- Where each file came from: [docs/provenance.md](docs/provenance.md)
- All docs: [docs/README.md](docs/README.md)

## MCP

```
cd mcp && python3 -m venv .venv && .venv/bin/pip install -e ".[test]"
```

- `frank-docs` (read-only): boards, pins, the C API, examples, gotchas, the console protocol.
- `frank-dev`: bench status, build, ELF lint, flash, console, frame/audio capture, new firmware.

## Libraries

| Library | Header | What |
|---|---|---|
| `frank_platform` | `frank/platform.h` | clocks, PSRAM, heap, console, reboot, hang recovery (always linked) |
| `frank_video` | `frank/video.h` | 320x240 8-bit on HDMI (HSTX/PIO), VGA or composite; HDMI audio |
| `frank_audio` | `frank/audio.h` | I2S (+TLV320), PWM, HDMI: one ring |
| `frank_storage` | `frank/storage.h` | microSD over SPI or SDIO, FatFs R0.15 |
| `frank_input` | `frank/input.h` | USB host (native or PIO-USB), PS/2, NES pads |
| `frank_link` | `frank/link.h` | the Next / Core 2 master-slave link |

## Status

Verified on FRANK Next: the platform, HDMI picture and sound, SDIO, the TLV320 at
44.1 kHz, the recovery path. Built and linted for every board, not yet run on hardware:
M1, M2, C2, FZ, FN, Z0, PC; VGA, composite, PWM audio, PS/2, NES pad, USB host, the link
(the Next's slave is not on USB on the bench).

## Licence

MIT, see [LICENSE](LICENSE). Third-party code keeps its own licence (listed there).
