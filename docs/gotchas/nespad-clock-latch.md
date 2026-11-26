# NES pad: clock GPIO20, latch GPIO21

**Symptom.** The gamepad dialog sees no button at all.

**Cause.** Swapped clock and latch: the 4021 got a latch pulse where it expected clocks.

**Fix.** Pins come from boards/m2.yaml (nespad.clk 20, latch 21). Never type them again.

**Found in:** frank-test.
