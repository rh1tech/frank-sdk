# The SDK's 4 KB main stack overflows into core 1's

**Symptom.** HDMI dies, or core 1 crashes, as soon as core 0 goes deep (a game's renderer).

**Cause.** The SDK puts core 0's stack in SCRATCH_Y (4 KB), right after core 1's in SCRATCH_X.

**Fix.** frank-sdk's linker script: 32 KB main stack in its own region at 0x20078000; core 1's stack in main RAM (frank_launch_core1, 16 KB, with a high-water mark).

**Found in:** frank-blood.
