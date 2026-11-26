# Core 2 / Next master: SWD at 1 MHz, no resets

**Symptom.** openocd reset/halt wedges the debug port; an interrupted SWD flash boot-looped beyond SWD rescue.

**Cause.** HDMI interference on the master's SWD; resets through the probe leave the chips in a bad state.

**Fix.** Flash with picotool over USB (frank flash). SWD only to read memory without halting, or for the boot ROM's reboot to BOOTSEL; 1 MHz on the master.

**Found in:** frank-react, frank-amiga.
