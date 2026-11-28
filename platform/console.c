/*
 * frank-sdk — the console protocol every FRANK firmware speaks.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 *   ~BOOT~        reboot into BOOTSEL (then picotool load -v -x)
 *   ~RST~         plain reboot
 *   ~VERB~        toggle frank_verbose
 *   ~PROF~        call profiler_rearm() if the firmware links one
 *   ~C line~      a command: the built-ins and frank_console_add()'s here,
 *                 anything else to the firmware's on_cmd
 * Anywhere in the byte stream: a firmware can pass every byte on as a key and
 * still be reflashed. From frank-ecat platform_uart_cmd and frank-diablo
 * platform_console_poll; the command table is new.
 */
#include "frank/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

volatile bool frank_verbose;
volatile bool frank_scripted;

#define MAX_CMDS 48
static struct {
    const char *name, *help;
    frank_cmd_fn fn;
} cmds[MAX_CMDS];
static int ncmds;

/* golden token lists and typed UTF-8 text (Athena) run long */
#ifndef FRANK_CONSOLE_LINE
#define FRANK_CONSOLE_LINE 1024
#endif
static char line[FRANK_CONSOLE_LINE];
static volatile bool line_ready;

void frank_console_add(const char *name, frank_cmd_fn fn, const char *help) {
    for (int i = 0; i < ncmds; i++)
        if (!strcmp(cmds[i].name, name)) {
            cmds[i].fn = fn;
            cmds[i].help = help;
            return;
        }
    if (ncmds < MAX_CMDS) {
        cmds[ncmds].name = name;
        cmds[ncmds].fn = fn;
        cmds[ncmds].help = help;
        ncmds++;
    }
}

/* a magic word matched one byte at a time */
static bool match(const char *word, unsigned *pos, int ch) {
    *pos = (ch == word[*pos]) ? *pos + 1 : (ch == word[0]);
    if (word[*pos]) return false;
    *pos = 0;
    return true;
}

void frank_console_feed(int ch) {
    static unsigned boot, rst, verb, prof;
    if (match("~BOOT~", &boot, ch)) {
        printf("\nrebooting to BOOTSEL\n");
        busy_wait_ms(20);
        frank_reboot_clean(true);
    }
    if (match("~RST~", &rst, ch)) {
        printf("\nrebooting\n");
        busy_wait_ms(20);
        frank_reboot_clean(false);
    }
    if (match("~VERB~", &verb, ch)) frank_verbose = !frank_verbose;
    if (match("~PROF~", &prof, ch)) {
        extern void profiler_rearm(void) __attribute__((weak));
        if (profiler_rearm) {
            printf("\nprofiling\n");
            profiler_rearm();
        }
    }
    /* "~C line~" */
    static char buf[sizeof(line)];
    static int st, len;
    if (st == 3) {
        if (ch == '~') {
            buf[len] = 0;
            st = 0;
            if (!line_ready) {
                memcpy(line, buf, (size_t)len + 1);
                line_ready = true;
            }
        } else if (len < (int)sizeof(buf) - 1) {
            buf[len++] = (char)ch;
        } else {
            st = 0;
        }
    } else if (ch == '~') {
        st = 1;
    } else if (st == 1 && ch == 'C') {
        st = 2;
    } else if (st == 2 && ch == ' ') {
        st = 3;
        len = 0;
        frank_scripted = true;
    } else {
        st = 0;
    }
}

/* true if a table entry ran the line (its name, a space, its arguments) */
static bool run_builtin(const char *l) {
    char name[24];
    size_t n = strcspn(l, " ");
    if (n >= sizeof(name)) return false;
    memcpy(name, l, n);
    name[n] = 0;
    const char *args = l[n] ? l + n + 1 : l + n;
    for (int i = 0; i < ncmds; i++)
        if (!strcmp(cmds[i].name, name)) {
            cmds[i].fn(args);
            printf("OK %s\n", name);   /* the bench scripts wait for this */
            return true;
        }
    return false;
}

int frank_console_poll(void (*on_char)(int ch), void (*on_cmd)(const char *line)) {
    int last = -1;
    for (int c; (c = getchar_timeout_us(0)) >= 0;) {
        frank_console_feed(c);
        if (on_char) on_char(c);
        last = c;
        /* a complete "~C ...~" runs before the next byte is read: a command
         * may read its own data from the console */
        if (line_ready) {
            char l[sizeof(line)];
            memcpy(l, line, sizeof(l));
            line_ready = false;
            if (!run_builtin(l) && on_cmd) on_cmd(l);
        }
    }
    return last;
}

/* ---- the console as keys: getchar_timeout_us() with the protocol taken out ----
 * For a firmware whose own code reads the console as keys (frank-test's UI):
 * linked with -Wl,--wrap=stdio_getchar_timeout_us (getchar_timeout_us() is an
 * inline alias of it, so no symbol to wrap) and FRANK_CONSOLE_WRAP_GETCHAR=1
 * (frank_console_wrap_getchar() in CMake), every reader then gets the bytes
 * that are not the protocol, and the protocol works wherever keys are read.
 * frank_console_poll() passes each byte on as well, which a key reader
 * cannot afford: the R of "~RST~" or the A of "~C help~" would press keys.
 * A byte that may start a sequence is held until the sequence completes or
 * breaks off; the bytes of a sequence come together, so the wait for the
 * next one is short. Held bytes that turn out not to be a sequence are
 * returned in order. */
#if FRANK_CONSOLE_WRAP_GETCHAR
int __real_stdio_getchar_timeout_us(uint32_t timeout_us);

#define SEQ_GAP_US 50000u   /* the longest wait for a sequence's next byte */

static char held[sizeof(line) + 4];
static unsigned held_n, held_out;

/* 1: a prefix of a sequence, 2: a whole one, 0: neither */
static int seq_state(const char *s, unsigned n) {
    static const char *const words[] = {"~BOOT~", "~RST~", "~VERB~", "~PROF~"};
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        const size_t wl = strlen(words[i]);
        if (n <= wl && !memcmp(s, words[i], n)) return n == wl ? 2 : 1;
    }
    if (n <= 3) return !memcmp(s, "~C ", n) ? 1 : 0;
    if (memcmp(s, "~C ", 3)) return 0;
    if (s[n - 1] == '~') return 2;
    return n < sizeof(line) + 3 ? 1 : 0;
}

static void seq_run(const char *s, unsigned n) {
    if (s[1] == 'C') {
        char l[sizeof(line)];
        memcpy(l, s + 3, n - 4);
        l[n - 4] = 0;
        frank_scripted = true;
        if (!run_builtin(l)) printf("unknown command: %s (~C help~)\n", l);
        return;
    }
    /* the magic words: frank_console_feed() knows what each one does */
    for (unsigned i = 0; i < n; i++) frank_console_feed((unsigned char)s[i]);
}

int __wrap_stdio_getchar_timeout_us(uint32_t timeout_us) {
    for (;;) {
        if (held_out < held_n) return (unsigned char)held[held_out++];
        held_n = held_out = 0;
        int c = __real_stdio_getchar_timeout_us(timeout_us);
        if (c != '~') return c;
        held[held_n++] = '~';
        int st = 1;
        while (st == 1) {
            c = __real_stdio_getchar_timeout_us(SEQ_GAP_US);
            if (c < 0) break;
            held[held_n++] = (char)c;
            st = seq_state(held, held_n);
        }
        if (st == 2) {
            seq_run(held, held_n);
            held_n = 0;
            return PICO_ERROR_TIMEOUT;   /* the sequence was all there was */
        }
        /* not a sequence (or it stopped coming): the held bytes, in order.
         * A sequence right behind a stray '~' is lost; resend it. */
    }
}
#endif

/* ---- built-in commands ---- */

extern void frank_report_boot(void);
extern void frank_crumb_report(void);

static void cmd_help(const char *a) {
    (void)a;
    printf("commands (~C name args~):\n");
    for (int i = 0; i < ncmds; i++) printf("  %-10s %s\n", cmds[i].name, cmds[i].help ? cmds[i].help : "");
    printf("  ~BOOT~ BOOTSEL, ~RST~ reboot, ~VERB~ verbose, ~PROF~ profile\n");
}
static void cmd_clocks(const char *a) { (void)a; frank_clocks_report(); }
static void cmd_mem(const char *a) { (void)a; frank_heap_report(); }
static void cmd_bootinfo(const char *a) { (void)a; frank_report_boot(); }
static void cmd_crumb(const char *a) { (void)a; frank_crumb_report(); }
static void cmd_prof(const char *a) {
    unsigned us = (unsigned)strtoul(a, NULL, 0);
    frank_prof_start(us ? us : 200);
}
static void cmd_core1(const char *a) {
    (void)a;
    printf("core 1 stack: %u bytes used\n", frank_core1_stack_used());
}
static void cmd_psram(const char *a) {
    (void)a;
    uint8_t mf, kgd;
    frank_psram_id(&mf, &kgd);
    printf("PSRAM CS%u: %u KB, ID %02X %02X%s, cap %u MHz, test %s\n", frank_psram_cs(),
           (unsigned)(frank_psram_size() / 1024), mf, kgd, kgd == 0x5D ? "" : " (expected 5D)",
           frank_psram_mhz_cap(), frank_psram_test() ? "ok" : "FAILED");
}

void frank_console_builtins(void) {
    frank_console_add("help", cmd_help, "this list");
    frank_console_add("clocks", cmd_clocks, "clk_sys, peri, hstx, flash and PSRAM timing, vreg");
    frank_console_add("mem", cmd_mem, "heap use, SRAM and PSRAM");
    frank_console_add("psram", cmd_psram, "PSRAM CS, size, ID, clock cap, a quick test");
    frank_console_add("bootinfo", cmd_bootinfo, "reset reason, failed boots, the last crash");
    frank_console_add("crumb", cmd_crumb, "the current crumb and main-loop count");
    frank_console_add("prof", cmd_prof, "[us] sample core 0's PC (PROF BEGIN..PROF END)");
    frank_console_add("core1", cmd_core1, "core 1 stack high-water mark");
}
