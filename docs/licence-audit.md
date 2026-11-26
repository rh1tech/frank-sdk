# Licence audit (phase 1, 2026-11-26)

frank-sdk is MIT.

**Decision (2026-11-26):** the author confirmed that all of this code is his, including the
pico-nes, pico-speccy, frank-msx and pico-infonesPlus lineage he maintains. Every file
below therefore goes into frank-sdk under MIT, and each gets the frank-sdk MIT header
when it is merged. Third-party code that keeps its own licence: FatFs (ChaN, BSD-1),
Elehobica SPI SD (BSD-2), SDIO_RP2350 (MIT), usb64 xinput (MIT), pico_hdmi (public domain).
Those keep their licence files next to them.

The table records where each driver came from.

| Driver | Origin | Upstream licence | Header today | Verdict |
|---|---|---|---|---|
| HSTX HDMI `psx_video`, `hstx_packet` | pico_hdmi | public domain | — | MIT OK |
| PIO HDMI `hdmi_pio` (quake2), `hdmi.c` (frank-dos, murm386) | frank-msx / pico-nes | GPL-3 | GPL-3 (quake2), none (dos) | own → MIT |
| frank-ecat HDMI | pico-speccy | GPL-3 | — | own → MIT |
| USB HID gamepad decoders (RIP parser, vendor layouts) | pico-speccy | GPL-3 | MIT | own → MIT |
| `xinput_host` | usb64 (Ryan Wendland) | MIT | MIT | MIT OK |
| NES pad | pico-infonesPlus | GPL-3 | MIT | own → MIT |
| VGA | pico-286 vga-nextgen (xrip) | MIT | MIT | MIT OK |
| PS/2 unified driver | unknown | ? | GPL-2+ | own → MIT |
| SDIO | SDIO_RP2350 (Rabbit Hole Computing) | MIT | MIT / GPL-3 wrapper | MIT OK (rewrap) |
| SPI SD (`sdcard.c`, `pio_spi`) | Elehobica | BSD-2 | BSD | OK |
| FatFs | ChaN | BSD-1 | — | OK |
| link, clocks, psram, platform, audio | own | — | GPL-2/3 | relicense to MIT |

Side finding: frank-dos and murm386 (MIT) ship the pico-nes-derived PIO HDMI driver.
