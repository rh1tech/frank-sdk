/*
 * frank-sdk — board bring-up, console, reboot and hang recovery.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * Boot order (all of it automatic; a firmware's main() calls frank_init()):
 *   runtime init "00001"  XIP cache emptied, core voltage back to 1.10 V
 *   runtime init "00499"  flash timing safe for the SDK's 150 MHz
 *   constructor 101       boot guard (3 failed boots -> BOOTSEL), 16 s watchdog,
 *                         Quad Enable, voltage + clk_sys, PSRAM, the heap -
 *                         before any C++ constructor touches .psram_bss
 *   frank_init()          stdio, the console, the boot report, safe mode after
 *                         a hang, the stall watcher
 *   frank_boot_ok()       the firmware is up: this boot counts as good
 * Then feed the watchdog and tick the crumb from the main loop.
 *
 * FRANK_RUNTIME_BOARD builds (generic targets GA, GB, or any target with
 * -DFRANK_RUNTIME_BOARD=ON): the constructor does nothing board-specific. The
 * firmware identifies the board, then calls frank_clocks_apply(),
 * frank_psram_init_cs() and frank_heap_add_psram() itself.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "frank/clocks.h"
#include "frank/psram.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- boot ---- */

/* stdio and the console (USB CDC: waits up to 3 s for the host to open it, so
 * the boot log is seen), the boot report, safe mode after a hang, the stall
 * watcher. `name` and `version` head the boot log. */
void frank_init(const char *name, const char *version);

/* The firmware is up: clears the boot guard. Until then a crash or a hang
 * counts as a failed boot; three in a row enter BOOTSEL. */
void frank_boot_ok(void);

/* ---- console: "~BOOT~" "~RST~" "~VERB~" "~PROF~" "~C line~" ---- */

/* Read every pending console byte: the protocol is handled here, each byte
 * is also passed to on_char (may be NULL), and a complete "~C line~" that is
 * not a built-in command goes to on_cmd (may be NULL). Call once per frame or
 * main-loop pass. Returns the last byte read, or -1. */
int frank_console_poll(void (*on_char)(int ch), void (*on_cmd)(const char *line));

/* Feed one received byte (frank_console_poll does it for stdio input). */
void frank_console_feed(int ch);

/* A "~C name args~" command. Built in: help, clocks, mem, bootinfo, crumb,
 * prof, core1. help lists every command with its help line (the MCP reads it). */
typedef void (*frank_cmd_fn)(const char *args);
void frank_console_add(const char *name, frank_cmd_fn fn, const char *help);

extern volatile bool frank_verbose;    /* "~VERB~" toggles it */
extern volatile bool frank_scripted;   /* a "~C" came: scripts drive the firmware */

/* ---- reboot: every software reboot goes through these ---- */

/* Core 1 forced off, interrupts off, PSRAM back to SPI mode, clocks and flash
 * timing as the boot ROM left them; then BOOTSEL or a watchdog reboot. */
void __attribute__((noreturn)) frank_reboot(bool bootsel);
/* The same, asked for (a command, a menu): not counted as a hang. */
void __attribute__((noreturn)) frank_reboot_clean(bool bootsel);

/* ---- hang recovery ---- */

/* The hardware watchdog (at most 16.7 s). frank_init() starts it at 16 s for
 * the boot; the firmware sets its own period and feeds it from the main loop.
 * After a watchdog reboot the next boot runs safe mode: it prints where the
 * firmware hung (crumb, the stall watcher's PCs) for 8 s, then BOOTSEL. */
void frank_watchdog_start(uint32_t ms);
void frank_watchdog_feed(void);
/* Where the firmware is: a stage number of the firmware's own and an argument.
 * Kept over the watchdog reboot. */
void frank_crumb(uint32_t stage, uint32_t arg);
/* Once per main-loop pass: the stall watcher counts these. */
void frank_crumb_tick(void);

/* ---- core 1 ---- */

/* Core 1 on a stack in main RAM (FRANK_CORE1_STACK, 16 KB): SCRATCH_X is left
 * to time-critical code. */
void frank_launch_core1(void (*entry)(void));
unsigned frank_core1_stack_used(void);

/* ---- profiling ---- */

void frank_cyccnt_enable(void);
static inline uint32_t frank_cyccnt(void) { return *(volatile uint32_t *)0xE0001004; }
/* 4096 samples of core 0's PC every `us` (jittered), then printed between
 * "PROF BEGIN n" and "PROF END" by frank_prof_dump_if_done() (tools/prof.py). */
void frank_prof_start(uint32_t us);
void frank_prof_pairs(bool pc_and_lr);
void frank_prof_core1_init(void);      /* on core 1: sample it too */
void frank_prof_start_core1(uint32_t us);
bool frank_prof_dump_if_done(void);

/* ---- heap: newlib's malloc on two dlmalloc spaces ---- */

/* Blocks up to FRANK_SRAM_SMALL (14 KB) come from SRAM, larger ones from
 * PSRAM; when one side is full the other is tried. These force a side. */
void *frank_sram_malloc(size_t n);
/* A run-time board (FRANK_RUNTIME_BOARD): give malloc the PSRAM that
 * frank_psram_init_cs() found. Until then everything comes from SRAM. */
void frank_heap_add_psram(size_t bytes);
void *frank_psram_malloc(size_t n);
void frank_heap_report(void);
size_t frank_sram_free(void);

/* ---- USB host task, bounded ---- */

/* Runs task() (tuh_task and the HID code) with a 1 s guard: a device that
 * never enumerates (another host on the port) kept TinyUSB looping inside
 * the call and froze core 0. Then the USB interrupt goes off and stays off
 * until the next boot. Call from the main loop, never from an interrupt. */
void frank_usb_poll(void (*task)(void));

#ifdef __cplusplus
}
#endif
