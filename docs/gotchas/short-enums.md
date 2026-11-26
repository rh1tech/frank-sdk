# Short enums: never in a struct shared with other code

**Symptom.** A struct read from game data or passed to a driver has the wrong layout; the linker warns about enum sizes.

**Cause.** arm-none-eabi uses short enums by default and newlib/the SDK are built so. Game code that reads struct layouts from data needs -fno-short-enums; mixing the two in one struct breaks silently.

**Fix.** frank_no_short_enums(target sources...) for such code only; shared structs use uint8_t/uint16_t, never an enum type.

**Found in:** frank-castle.
