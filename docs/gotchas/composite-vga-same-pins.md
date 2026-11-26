# HDMI, VGA and composite share GPIO12-19 on the M2 family

**Symptom.** A driver drives a connector the board does not have, or two drivers fight over the pins.

**Cause.** FRANK, MegaFRANK, PGA: the same eight pins feed HDMI, the VGA resistor ladder, and the composite DAC (sync on base + 6). The detector cannot tell which is connected.

**Fix.** One video output per build (a `shared` group in boards/m2.yaml). Next, Core 2, FZ and Z0 have HDMI only.

**Found in:** frank-test.
