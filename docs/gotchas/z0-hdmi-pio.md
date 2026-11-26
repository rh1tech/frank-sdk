# Waveshare PiZero (Z0): HDMI only through frank-quake2's PIO driver

**Symptom.** No signal on Z0 although the PIO stream looks valid.

**Cause.** Its HDMI pins (GPIO32-39) are out of HSTX's reach; pico-speccy's PIO back-end never locks a sink there (cause not found).

**Fix.** frank-sdk uses hdmi_pio (frank-quake2) for Z0: 320x240 8-bit doubled, audio in the blanking. It takes all of SCRATCH_X and part of SCRATCH_Y.

**Found in:** frank-ecat, frank-quake2.
