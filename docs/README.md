# frank-sdk documentation

| Page | What |
|---|---|
| [new-firmware.md](new-firmware.md) | a new firmware from the template, the shape of `main()` |
| [building.md](building.md) | the libraries, the build options, the desktop build |
| [bench.md](bench.md) | flashing, the console, picture and sound checks, recovery |
| [boards/](boards) | one page per target: pins, clocks, console, PIO plan (generated) |
| [gotchas/](gotchas/README.md) | the fleet's lessons: symptom, cause, fix |
| [provenance.md](provenance.md) | where every file came from, what was merged in, what was verified on hardware |
| [inventory.md](inventory.md) | the drift between the fleet's copies (`tools/inventory.py`) |
| [licence-audit.md](licence-audit.md) | the origin and licence of each driver |
| [migrations/frank-test.md](migrations/frank-test.md) | moving frank-test onto frank-sdk: the plan |

The C API is documented in its headers: `platform/include/frank/*.h`,
`drivers/*/include/frank/*.h` (the frank-docs MCP serves them with `api`).
