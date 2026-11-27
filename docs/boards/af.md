# Adafruit Fruit Jam — target `AF`

Generated from [`boards/af.yaml`](../../boards/af.yaml) by `tools/gen_boards.py`. Do not edit.

- **MCU:** RP2350B, role single
- **Covers:** Adafruit Fruit Jam
- **Flash:** 16M
- **PSRAM:** 8M on CS GPIO47
- **Clocks:** CPU 252 MHz at 1.1 V, PSRAM ≤ 133 MHz, flash ≤ 66 MHz
- **Console:** USB CDC by default; UART1 TX GPIO8 / RX GPIO9
- **HDMI:** HSTX on GPIO12-19 (clock pair GPIO12)
- **PIO:** sd → PIO0, i2s → PIO1, pio_usb → PIO2; PIO0 base 16, PIO1 base 0, PIO2 base 0

| GPIO | Function |
|---:|---|
| 0 | gpio.button1 |
| 1 | usb.pio_usb |
| 2 | usb.pio_usb |
| 3 | gpio.esp_busy |
| 4 | gpio.button2 |
| 5 | gpio.button3 |
| 8 | console.uart |
| 9 | console.uart |
| 11 | usb.pio_usb_5v_en |
| 12 | video.hdmi |
| 13 | video.hdmi |
| 14 | video.hdmi |
| 15 | video.hdmi |
| 16 | video.hdmi |
| 17 | video.hdmi |
| 18 | video.hdmi |
| 19 | video.hdmi |
| 20 | i2c |
| 21 | i2c |
| 22 | audio.i2s |
| 23 | gpio.esp_irq |
| 24 | audio.i2s |
| 25 | audio.i2s |
| 26 | audio.i2s |
| 27 | audio.i2s |
| 28 | gpio.esp_miso |
| 29 | led |
| 30 | gpio.esp_sck |
| 31 | gpio.esp_mosi |
| 32 | led |
| 33 | gpio.sd_detect |
| 34 | sd / sd_spi_fallback |
| 35 | sd / sd_spi_fallback |
| 36 | sd / sd_spi_fallback |
| 37 | sd |
| 38 | sd |
| 39 | sd / sd_spi_fallback |
| 46 | gpio.esp_cs |
| 47 | psram |

Shared (a build uses one of each group): sd vs sd_spi_fallback

Sources: pico-sdk 2.2.0 src/boards/include/boards/adafruit_fruit_jam.h; adafruit/circuitpython ports/raspberrypi/boards/adafruit_fruit_jam (mpconfigboard.h, pins.c, board.c)
