# Run tuh_task from the main loop, bounded

**Symptom.** Core 0 freezes for good when another host (a PC) is on the USB-C port.

**Cause.** TinyUSB enumerates with blocking delays; a device that never enumerates kept it looping inside a timer interrupt.

**Fix.** frank_usb_poll(task): from the main loop, with a 1 s alarm that turns the USB interrupt off if a call blocks.

**Found in:** frank-ecat, frank-diablo.
