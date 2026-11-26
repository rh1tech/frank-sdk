# SDK 2.2 runs .preinit_array again on core 1

**Symptom.** Bus faults at core 1's launch, C++ objects wiped.

**Cause.** Every .preinit_array entry after the SDK's marker also runs on core 1 when it starts; a PSRAM init there re-programs the QMI under core 0 and zeroes .psram_bss.

**Fix.** frank-sdk's early init is a constructor (priority 101) that returns at once on core 1. A numeric preinit priority avoids core 1 but runs before the coprocessor enable (float code faults with NOCP).

**Found in:** frank-blood.
