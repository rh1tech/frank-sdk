# The console is the board's own USB CDC, not the probe's UART

**Symptom.** Nothing comes out of the debug probe's serial port; ~BOOT~ sent there does nothing.

**Cause.** On the M2 and FRANK Next benches the probe's UART is not wired to the board. The console is the firmware's USB CDC on the native port (FRANK_CONSOLE=usb).

**Fix.** Build dev images with the USB console (the board files' default where it applies). Address the board by its USB serial (bench.yaml), never by tty name or the probe's port. A HID build (native USB = host) has no USB console: test on a console build first.

**Found in:** frank-diablo, frank-war1, frank-war2, frank-react bench notes.
