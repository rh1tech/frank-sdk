/*
 * frank-sdk: psram_unstick (see CMakeLists.txt). Runs from RAM only: the flash
 * is what is in trouble.
 * SPDX-License-Identifier: MIT
 */
#include "hardware/gpio.h"
#include "hardware/structs/sysinfo.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

int main(void) {
    const bool rp2350a = *((io_ro_32 *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET)) & 1u;
    unsigned cs = FRANK_PSRAM_CS;
#ifdef FRANK_PSRAM_CS_RP2350A
    if (rp2350a) cs = FRANK_PSRAM_CS_RP2350A;
#endif
#ifdef FRANK_PSRAM_CS_RP2350B
    if (!rp2350a) cs = FRANK_PSRAM_CS_RP2350B;
#endif
    (void)rp2350a;
    gpio_init(cs);
    gpio_set_dir(cs, GPIO_OUT);
    gpio_put(cs, 1);            /* deselect: the part lets go of the bus */
    busy_wait_ms(200);
    reset_usb_boot(0, 0);       /* back to BOOTSEL: the flash loads verify again */
    while (1) tight_loop_contents();
}
