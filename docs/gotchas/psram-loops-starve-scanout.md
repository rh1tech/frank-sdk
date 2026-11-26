# Tight core-0 PSRAM loops starve a PSRAM scan-out

**Symptom.** Bands of the picture shifted by a few rows while core 0 copies or fills PSRAM.

**Cause.** Core 1's row fetches from PSRAM wait behind core 0's QMI traffic.

**Fix.** Frame buffers in SRAM (the frank_video mode is 320x240 in SRAM); where a screen must live in PSRAM, pace the loop per row (frank-castle's Video_PsramPace) and never redraw periodically.

**Found in:** frank-castle.
