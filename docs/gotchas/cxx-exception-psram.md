# A C++ exception must not live in PSRAM

**Symptom.** The first exception thrown after the SRAM heap fills spins in __cxa_begin_catch for ever.

**Cause.** libsupc++ counts references with LDREX/STREX, and a STREX to PSRAM never succeeds.

**Fix.** frank-sdk wraps __cxa_allocate_exception: SRAM only (malloc's NULL then sends libsupc++ to its emergency pool).

**Found in:** frank-ecat.
