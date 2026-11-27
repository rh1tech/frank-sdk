# Olimex RP2350pc — target `OL`

Generated from [`boards/ol.yaml`](../../boards/ol.yaml) by `tools/gen_boards.py`. Do not edit.

- **MCU:** RP2350B, role single
- **Covers:** Olimex RP2350pc
- **Flash:** 16M
- **PSRAM:** 8M on CS GPIO8
- **Clocks:** CPU 504 MHz at 1.65 V, PSRAM ≤ 133 MHz, flash ≤ 66 MHz
- **Console:** USB CDC by default; UART0 TX GPIO0 / RX GPIO1
- **HDMI:** HSTX on GPIO12-19 (clock pair GPIO14)
- **PIO:** i2s → PIO0; PIO0 base 0

| GPIO | Function |
|---:|---|
| 0 | console.uart |
| 1 | console.uart |
| 8 | psram |
| 9 | sd |
| 10 | sd |
| 11 | sd |
| 12 | video.hdmi |
| 13 | video.hdmi |
| 14 | video.hdmi |
| 15 | video.hdmi |
| 16 | video.hdmi |
| 17 | video.hdmi |
| 18 | video.hdmi |
| 19 | video.hdmi |
| 20 | gpio.hdmi_cec |
| 21 | gpio.hdmi_hpd |
| 22 | gpio.codec_power_off |
| 23 | audio.i2s |
| 24 | sd |
| 25 | led |
| 28 | gpio.codec_asdout |
| 29 | audio.i2s |
| 30 | audio.i2s |
| 31 | audio.i2s |
| 32 | i2c |
| 33 | i2c |
| 35 | gpio.amp_mute |

Sources: Olimex github.com/OLIMEX/RP2350pc HARDWARE/RP2350pc-Rev.A (schematic); frank-test boards/fleet/rp2350pc.yaml (HDMI pair order, SD, ES8311, on hardware); frank-dos (SD on SPI1)
