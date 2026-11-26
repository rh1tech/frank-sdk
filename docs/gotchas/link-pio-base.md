# The master-slave link needs a whole PIO at GPIO base 16

**Symptom.** The slave never answers; it looks dead.

**Cause.** Bus B reaches GPIO39, so its PIO instance needs pio_set_gpio_base(16), which makes pins below 16 unreachable on that instance. Sharing it with HDMI/I2S/SD fails quietly (PICO_ERROR_BAD_ALIGNMENT).

**Fix.** The board files give the link its own PIO (NX: PIO2, C2: PIO0); gen_boards.py refuses a PIO whose pins do not fit one 32-pin window. Both halves run the same clk_sys (checked too).

**Found in:** frank-dos, frank-react, frank-blood.
