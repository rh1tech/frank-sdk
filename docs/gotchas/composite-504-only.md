# Composite runs at 504 MHz only

**Symptom.** A composite build at 378 MHz: the picture is fine on a still screen and flickers in a game: single black lines, a few dozen a second, more in the lower half.

**Cause.** tv-software encodes every line in a 30 kHz timer interrupt on core 1, which takes most of that core at 378 MHz. A firmware with its frame in PSRAM also copies each of the 240 rows into SRAM on core 1 once a field (16.7 ms). With the game keeping the PSRAM busy on core 0, the copies do not always finish before the encoder needs the row: it is shown black. Measured on FRANK PGA with frank-idtech1: 34 late rows a second at 378 MHz (PSRAM 126 MHz), none at 504 MHz (PSRAM 168 MHz). Bus priority for core 1 and a longer row ring did not help.

**Fix.** clk_sys 504 MHz for composite: `frank_video.c` stops the build below it (`FRANK_CPU_MHZ < 504`).

**Found in:** frank-idtech1 (2026-11-27).
