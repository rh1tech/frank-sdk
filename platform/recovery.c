/*
 * frank-sdk — reboot, crash capture, boot guard, watchdog, stall watcher and
 * safe mode: a board that hangs can always be reflashed with nobody at it.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * From frank-ecat platform.c (reboot, crumbs, stall watcher, safe mode, crash
 * capture) and frank-diablo platform.c (the boot guard, "reboot asked for
 * from" in the watchdog scratch registers).
 */
#include "frank/platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hardware/irq.h"
#include "hardware/structs/powman.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/timer.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

/* ---- state that survives a reboot (not a power cycle) ---- */

/* crumbs: magic, stage, arg, main-loop passes */
static volatile uint32_t __attribute__((section(".uninitialized_data"))) crumbs[4];
#define CRUMB_MAGIC 0xC4B5A000u
/* where core 0 stood when the stall watcher saw the main loop stop */
static volatile uint32_t __attribute__((section(".uninitialized_data"))) stall_pc[3];
#define STALL_RING 24
static volatile uint32_t __attribute__((section(".uninitialized_data"))) stall_ring[STALL_RING][2];
static volatile uint32_t __attribute__((section(".uninitialized_data"))) stall_ring_n;
static volatile uint32_t __attribute__((section(".uninitialized_data"))) crash_info[12];
#define CRASH_HARDFAULT 0xDEADBEEFu
#define CRASH_PANIC     0xDEADCAFEu

/* Watchdog scratch registers (they survive every reset but a power cycle):
 *   [0] boot guard: BOOT_MAGIC | in-progress 0x100 | failed boots
 *   [5] [6] the last asked-for reboot: caller, 0x5EB00700 | core */
#define BOOT_MAGIC 0xF4A10000u
#define BOOT_WATCHDOG_MS 16000u   /* the RP2350 watchdog tops out at 16.7 s */
#define REBOOT_TAG 0x5EB00700u

/* ---- reboot ---- */

static bool guard_on;   /* frank_boot_guard() ran this boot: scratch[0] is its */

void __attribute__((noreturn)) frank_reboot(bool bootsel) {
    fflush(stdout);
    /* the console's last words out first, but not from a fault handler: the
     * USB stack may be what faulted */
    if (__get_current_exception() == 0) {
        stdio_flush();
        busy_wait_ms(20);
    }
    if (get_core_num() == 0) {
        /* Core 1 off: it may be reading PSRAM. Not multicore_reset_core1(),
         * which waits for core 1's boot ROM on the FIFO and hung there once.
         * Released again at once: it restarts into the boot ROM (which waits
         * for a launch and does not touch PSRAM); left forced off, the bit
         * survives the watchdog reboot and the chip does not come back. */
        hw_set_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
        while (!(psm_hw->frce_off & PSM_FRCE_OFF_PROC1_BITS)) tight_loop_contents();
        busy_wait_us(10);
        hw_clear_bits(&psm_hw->frce_off, PSM_FRCE_OFF_PROC1_BITS);
        busy_wait_us(10);
    }
    save_and_disable_interrupts();
    frank_psram_reset_to_spi();
    frank_clocks_for_reboot();
    if (bootsel) reset_usb_boot(0, 0);
    watchdog_reboot(0, 0, 10);
    while (1) tight_loop_contents();
}

void __attribute__((noreturn)) frank_reboot_clean(bool bootsel) {
    watchdog_hw->scratch[5] = (uint32_t)(uintptr_t)__builtin_return_address(0);
    watchdog_hw->scratch[6] = REBOOT_TAG | get_core_num();
    crumbs[0] = 0;
    /* not a failed boot. Only with the guard on: a run-time board without it
     * keeps scratch[0] for itself (frank-test's overclock campaign) */
    if (guard_on) watchdog_hw->scratch[0] = BOOT_MAGIC;
    frank_reboot(bootsel);
}

/* ---- crash capture ---- */

void __real_panic(const char *fmt, ...);

void __attribute__((noreturn)) __wrap_panic(const char *fmt, ...) {
    /* TinyUSB host asserts during a flaky enumeration are not worth a reboot:
     * park this core, the other keeps running */
    if (fmt && (strstr(fmt, "Host") || strstr(fmt, "HCD") || strstr(fmt, "tuh") || strstr(fmt, "EP")))
        while (1) tight_loop_contents();
    void *lr;
    __asm volatile("mov %0, lr" : "=r"(lr));
    crash_info[0] = CRASH_PANIC;
    crash_info[1] = (uint32_t)(uintptr_t)lr;
    crash_info[2] = (uint32_t)(uintptr_t)fmt;
    crash_info[3] = get_core_num();
    printf("\n*** PANIC on core %u ***\n", (unsigned)crash_info[3]);
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        printf("\n");
    }
    busy_wait_ms(100);
    frank_reboot(false);
}

void __attribute__((used)) frank_hardfault_c(uint32_t *frame) {
    crash_info[0] = CRASH_HARDFAULT;
    crash_info[1] = frame[6];                         /* PC */
    crash_info[2] = frame[5];                         /* LR */
    crash_info[3] = frame[0];
    crash_info[4] = frame[1];
    crash_info[5] = *(volatile uint32_t *)0xE000ED28; /* CFSR */
    crash_info[6] = *(volatile uint32_t *)0xE000ED2C; /* HFSR */
    crash_info[7] = *(volatile uint32_t *)0xE000ED38; /* BFAR */
    crash_info[8] = get_core_num();
    printf("\n*** HARDFAULT core %u PC=0x%08x LR=0x%08x CFSR=0x%08x BFAR=0x%08x ***\n",
           (unsigned)crash_info[8], (unsigned)crash_info[1], (unsigned)crash_info[2],
           (unsigned)crash_info[5], (unsigned)crash_info[7]);
    busy_wait_ms(500);
    frank_reboot(false);
}

void __attribute__((naked, used)) isr_hardfault(void) {
    __asm volatile("tst lr, #4\n"
                   "ite eq\n"
                   "mrseq r0, msp\n"
                   "mrsne r0, psp\n"
                   "b frank_hardfault_c\n");
}

/* ---- boot guard (constructor 101 calls frank_boot_guard) ---- */

static unsigned boot_failures;
static uint32_t boot_chip_reset, boot_wd_reason, last_crash[12];
static uint32_t asked_from, asked_tag;

static bool boot_captured;

/* why this boot happened: read once, at the guard or (without it) the report */
static void boot_capture(void) {
    if (boot_captured) return;
    boot_captured = true;
    boot_chip_reset = powman_hw->chip_reset;
    boot_wd_reason = watchdog_hw->reason;
    asked_from = watchdog_hw->scratch[5];
    asked_tag = watchdog_hw->scratch[6];
    watchdog_hw->scratch[6] = 0;
}

void frank_boot_guard(void) {
    guard_on = true;
    boot_capture();
    uint32_t tag = watchdog_hw->scratch[0];
    uint32_t fails = 0;
    if ((tag & 0xFFFF0000u) == BOOT_MAGIC) {
        fails = tag & 0xFF;
        if (tag & 0x100) fails++;   /* the last boot never got to frank_boot_ok() */
    }
    boot_failures = fails;
    if (fails >= 3) {
        watchdog_hw->scratch[0] = BOOT_MAGIC;   /* counted from 0 again */
        reset_usb_boot(0, 0);
    }
    watchdog_hw->scratch[0] = BOOT_MAGIC | 0x100 | fails;
    watchdog_enable(BOOT_WATCHDOG_MS, true);
}

void frank_boot_ok(void) {
    if (guard_on) watchdog_hw->scratch[0] = BOOT_MAGIC;
}

void frank_report_boot(void) {
    static const struct { uint32_t bit; const char *name; } why[] = {
        {1u << 28, "watchdog(rsm)"}, {1u << 27, "hazard-sysresetreq"}, {1u << 26, "glitch"},
        {1u << 25, "swcore-pd"}, {1u << 24, "watchdog(swcore)"}, {1u << 23, "watchdog(powman)"},
        {1u << 22, "watchdog(powman-async)"}, {1u << 21, "rescue"}, {1u << 19, "debug-port"},
        {1u << 18, "RUN pin"}, {1u << 17, "brown-out"}, {1u << 16, "power-on"},
    };
    boot_capture();
    static bool crash_taken;
    if (!crash_taken) {   /* the report can be asked for again (~C bootinfo~) */
        crash_taken = true;
        for (int i = 0; i < 12; i++) last_crash[i] = crash_info[i];
        crash_info[0] = 0;
    }
    printf("boot: reset by");
    for (unsigned i = 0; i < sizeof(why) / sizeof(why[0]); i++)
        if (boot_chip_reset & why[i].bit) printf(" %s", why[i].name);
    /* CHIP_RESET bits are sticky since power-on; the watchdog's REASON is per reset */
    printf(" (CHIP_RESET 0x%08lx); watchdog: %s\n", (unsigned long)boot_chip_reset,
           (boot_wd_reason & 2u) ? "forced (a software reboot)"
           : (boot_wd_reason & 1u) ? "timed out (a hang, or picotool's restart after a load)" : "not the cause");
    if ((asked_tag & 0xFFFFFF00u) == REBOOT_TAG)
        printf("boot: the last reboot was asked for from 0x%08lx on core %lu\n",
               (unsigned long)asked_from, (unsigned long)(asked_tag & 0xFF));
    if (boot_failures) printf("boot: %u failed boot(s) before this one (3 enter BOOTSEL)\n", boot_failures);
    if (last_crash[0] == CRASH_HARDFAULT)
        printf("boot: after a HardFault on core %lu at PC=0x%08lx LR=0x%08lx CFSR=0x%08lx BFAR=0x%08lx\n",
               (unsigned long)last_crash[8], (unsigned long)last_crash[1], (unsigned long)last_crash[2],
               (unsigned long)last_crash[5], (unsigned long)last_crash[7]);
    else if (last_crash[0] == CRASH_PANIC)
        printf("boot: after a panic on core %lu, LR=0x%08lx\n", (unsigned long)last_crash[3],
               (unsigned long)last_crash[1]);
}

/* ---- watchdog and crumbs ---- */

static bool watchdog_on = true;   /* the boot guard started it */

void frank_watchdog_start(uint32_t ms) {
    watchdog_enable(ms, true);   /* paused while a debugger halts the core */
    watchdog_on = true;
}

void frank_watchdog_feed(void) {
    if (watchdog_on) watchdog_update();
}

void frank_crumb(uint32_t stage, uint32_t arg) {
    crumbs[0] = CRUMB_MAGIC;
    crumbs[1] = stage;
    crumbs[2] = arg;
}

void frank_crumb_tick(void) {
    crumbs[3]++;
}

void frank_crumb_report(void) {
    printf("crumb: stage %lu arg 0x%08lx, %lu main loops\n", (unsigned long)crumbs[1],
           (unsigned long)crumbs[2], (unsigned long)crumbs[3]);
}

/* ---- stall watcher and the sampling profiler ----
 * TIMER1's alarm 0 every 100 ms, a raw handler that reads the interrupted PC
 * and LR off the exception frame (TIMER1 is unused by the SDK, whose alarm
 * pool is on TIMER0). When the main-loop count has not moved for 1 s the
 * sample is kept for safe mode, and from 0.5 s on a ring of them: where a
 * hang goes round, not one point of it. 1 s, so a firmware's watchdog of 2 s
 * or more still lets it record. The same tick, faster, is the profiler. */
#define STALL_IRQ TIMER1_IRQ_0
#define STALL_US  100000u
#define PROF_SAMPLES 4096
static uint32_t stall_seen_tick, stall_count;
static uint32_t *prof_buf;
static volatile uint32_t prof_n;
static volatile bool prof_pairs, prof_core1;
static volatile uint32_t sample_us = STALL_US, sample1_us = 200;
static uint32_t stall_div, stall_acc;

void __attribute__((used)) __not_in_flash_func(frank_stall_isr_c)(uint32_t *frame) {
    timer1_hw->intr = 1u;
    /* jittered while profiling: a period locked to the firmware's own timers
     * samples them and nothing else */
    static uint32_t lfsr = 0xACE1u;
    lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
    timer1_hw->alarm[0] = timer1_hw->timerawl + sample_us + ((prof_buf && prof_n < PROF_SAMPLES) ? (lfsr & 63u) : 0u);
    if (prof_buf && prof_n < PROF_SAMPLES) {
        prof_buf[prof_n++] = frame[6];
        if (prof_pairs && prof_n < PROF_SAMPLES) prof_buf[prof_n++] = frame[5];
    }
    if (stall_div > 1 && ++stall_acc < stall_div) return;   /* the stall check stays at 100 ms */
    stall_acc = 0;
    if (crumbs[3] != stall_seen_tick) {
        stall_seen_tick = crumbs[3];
        stall_count = 0;
        return;
    }
    ++stall_count;
    if (stall_count == 5) stall_ring_n = 0;
    if (stall_count >= 5 && stall_ring_n < STALL_RING) {
        stall_ring[stall_ring_n][0] = frame[6];
        stall_ring[stall_ring_n][1] = frame[5];
        stall_ring_n++;
    }
    if (stall_count == 10) {
        stall_pc[0] = 0x57A11000u;
        stall_pc[1] = frame[6];
        stall_pc[2] = frame[5];
    }
}

/* in RAM, the handlers too: a stall worth recording is often one where XIP is
 * the trouble (frank check flagged them in flash) */
static void __attribute__((naked, section(".time_critical.stall_isr"))) stall_isr(void) {
    __asm volatile("tst lr, #4\n"
                   "ite eq\n"
                   "mrseq r0, msp\n"
                   "mrsne r0, psp\n"
                   "b frank_stall_isr_c\n");
}

void __attribute__((used)) __not_in_flash_func(frank_prof1_isr_c)(uint32_t *frame) {
    timer1_hw->intr = 2u;
    if (!prof_core1 || !prof_buf || prof_n >= PROF_SAMPLES) return;
    static uint32_t lfsr = 0x1D2Bu;
    lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
    timer1_hw->alarm[1] = timer1_hw->timerawl + sample1_us + (lfsr & 63u);
    prof_buf[prof_n++] = frame[6];
    if (prof_pairs && prof_n < PROF_SAMPLES) prof_buf[prof_n++] = frame[5];
}

static void __attribute__((naked, section(".time_critical.prof1_isr"))) prof1_isr(void) {
    __asm volatile("tst lr, #4\n"
                   "ite eq\n"
                   "mrseq r0, msp\n"
                   "mrsne r0, psp\n"
                   "b frank_prof1_isr_c\n");
}

void frank_stall_watch_start(void) {
    stall_pc[0] = 0;
    stall_seen_tick = crumbs[3];
    irq_set_exclusive_handler(STALL_IRQ, stall_isr);
    irq_set_priority(STALL_IRQ, 0x40);
    timer1_hw->alarm[0] = timer1_hw->timerawl + STALL_US;
    timer1_hw->inte |= 1u;
    irq_set_enabled(STALL_IRQ, true);
}

void frank_prof_core1_init(void) {
    irq_set_exclusive_handler(TIMER1_IRQ_1, prof1_isr);
    irq_set_priority(TIMER1_IRQ_1, 0x40);
    timer1_hw->inte |= 2u;
    irq_set_enabled(TIMER1_IRQ_1, true);
}

void frank_prof_pairs(bool on) { prof_pairs = on; }

void frank_prof_start(uint32_t us) {
    if (!prof_buf) prof_buf = (uint32_t *)frank_sram_malloc(PROF_SAMPLES * sizeof(uint32_t));
    if (!prof_buf) prof_buf = (uint32_t *)frank_psram_malloc(PROF_SAMPLES * sizeof(uint32_t));
    if (!prof_buf) {
        printf("profiling: no memory\n");
        return;
    }
    if (us < 20) us = 20;
    prof_n = 0;
    stall_div = STALL_US / us;
    sample_us = us;
    printf("profiling: %u samples every %lu us\n", PROF_SAMPLES, (unsigned long)us);
}

void frank_prof_start_core1(uint32_t us) {
    frank_prof_start(us);
    sample_us = STALL_US;   /* core 0 keeps its plain stall watch */
    stall_div = 0;
    sample1_us = us;
    prof_core1 = true;
    timer1_hw->alarm[1] = timer1_hw->timerawl + us;
}

bool frank_prof_dump_if_done(void) {
    if (!prof_buf || prof_n != PROF_SAMPLES) return false;
    prof_core1 = false;
    sample_us = STALL_US;
    stall_div = 0;
    printf("PROF BEGIN %u\n", PROF_SAMPLES);
    for (uint32_t i = 0; i < PROF_SAMPLES; i += 8) {
        for (uint32_t k = 0; k < 8; k++) printf("%08lx ", (unsigned long)prof_buf[i + k]);
        printf("\n");
        frank_watchdog_feed();
    }
    printf("PROF END\n");
    prof_n = PROF_SAMPLES + 1;   /* done until the next start */
    return true;
}

/* ---- safe mode ----
 * After a watchdog reboot with a crumb set (a hang): say where it hung, over
 * and over for 8 s so a console that opens late still sees it, then BOOTSEL.
 * Returns at once otherwise. */
void frank_safe_mode_if_hung(void) {
    if (!watchdog_caused_reboot() || crumbs[0] != CRUMB_MAGIC) {
        crumbs[0] = 0;
        crumbs[3] = 0;
        return;
    }
    const uint32_t stage = crumbs[1], arg = crumbs[2], ticks = crumbs[3];
    const bool have_pc = stall_pc[0] == 0x57A11000u;
    const uint32_t pc = stall_pc[1], lr = stall_pc[2];
    const uint32_t ring_n = stall_ring_n <= STALL_RING ? stall_ring_n : 0;
    crumbs[0] = 0;
    stall_pc[0] = 0;
    stall_ring_n = 0;
    for (int i = 0; i < 16; i++) {
        watchdog_update();
        if (i == 4 && have_pc)
            for (uint32_t k = 0; k < ring_n; k++)
                printf("*** stall %2lu: PC=0x%08lx LR=0x%08lx ***\n", (unsigned long)k,
                       (unsigned long)stall_ring[k][0], (unsigned long)stall_ring[k][1]);
        printf("*** SAFE MODE: the last run hung at stage %lu (arg 0x%08lx) after %lu main loops ***\n",
               (unsigned long)stage, (unsigned long)arg, (unsigned long)ticks);
        if (have_pc) printf("*** stalled at PC=0x%08lx LR=0x%08lx ***\n", (unsigned long)pc, (unsigned long)lr);
        printf("*** entering BOOTSEL in %d s ***\n", (16 - i) / 2);
        sleep_ms(500);
    }
    frank_reboot_clean(true);
}

/* ---- core 1 ---- */

#ifndef FRANK_CORE1_STACK
#define FRANK_CORE1_STACK (16 * 1024)
#endif
static uint32_t core1_stack[FRANK_CORE1_STACK / 4];

void frank_launch_core1(void (*entry)(void)) {
    for (unsigned i = 0; i < FRANK_CORE1_STACK / 4; i++) core1_stack[i] = 0x57AC57ACu;
    multicore_launch_core1_with_stack(entry, core1_stack, sizeof(core1_stack));
}

unsigned frank_core1_stack_used(void) {
    unsigned i = 0;
    while (i < FRANK_CORE1_STACK / 4 && core1_stack[i] == 0x57AC57ACu) i++;
    return (FRANK_CORE1_STACK / 4 - i) * 4;
}

void frank_cyccnt_enable(void) {
    *(volatile uint32_t *)0xE000EDFC |= (1u << 24);   /* DEMCR.TRCENA */
    *(volatile uint32_t *)0xE0001004 = 0;             /* CYCCNT */
    *(volatile uint32_t *)0xE0001000 |= 1u;           /* DWT_CTRL.CYCCNTENA */
}

/* ---- the USB host task, bounded ---- */

static volatile bool usb_dead;

static int64_t usb_guard_cb(alarm_id_t id, void *user) {
    (void)id;
    (void)user;
    usb_dead = true;
    irq_set_enabled(USBCTRL_IRQ, false);
    return 0;
}

void frank_usb_poll(void (*task)(void)) {
    if (usb_dead || !task) return;
    alarm_id_t guard = add_alarm_in_ms(1000, usb_guard_cb, NULL, true);
    task();
    if (guard > 0) cancel_alarm(guard);
    if (usb_dead) printf("USB host: the device on the port does not enumerate, host off until reboot\n");
}
