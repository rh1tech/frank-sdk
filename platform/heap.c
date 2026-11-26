/*
 * frank-sdk — newlib's malloc on two dlmalloc spaces: the SRAM after .bss and
 * the PSRAM after .psram_bss. free/realloc dispatch on the address.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * From frank-ecat ecat_malloc.c (itself from frank-quake2's q2_malloc.c).
 * Blocks up to FRANK_SRAM_SMALL go to SRAM, larger ones to PSRAM; when one
 * side is full the other is tried. Before PSRAM is up everything comes from
 * SRAM. Both cores may allocate: every entry point holds a hardware spinlock,
 * with interrupts left on - a PSRAM allocation walks dlmalloc's lists for
 * 40-90 us, and an HDMI line IRQ waiting behind it lost the picture for good.
 * So no interrupt handler may allocate.
 */
#include <errno.h>
#include <reent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "frank/platform.h"
#include "hardware/sync.h"

#ifndef FRANK_SRAM_SMALL
#define FRANK_SRAM_SMALL (14 * 1024)
#endif

typedef void *mspace;
extern mspace create_mspace_with_base(void *base, size_t capacity, int locked);
extern void *mspace_malloc(mspace msp, size_t bytes);
extern void *mspace_memalign(mspace msp, size_t alignment, size_t bytes);
extern void mspace_free(mspace msp, void *ptr);
extern size_t mspace_usable_size(const void *ptr);

extern uint8_t end[];          /* end of .bss */
extern uint8_t __HeapLimit[];  /* end of the RAM region */
extern uint8_t __psram_heap_start__[];

static mspace sram_msp, psram_msp;
static size_t sram_size, sram_used, sram_peak;
static size_t psram_size, psram_used, psram_peak;
static uintptr_t psram_end;

static inline bool is_psram(const void *p) {
    uintptr_t a = (uintptr_t)p;
    return a >= FRANK_PSRAM_BASE && a < FRANK_PSRAM_BASE + (16u << 20);
}

static void sram_init(void) {
    uintptr_t b = ((uintptr_t)end + 15) & ~(uintptr_t)15;
    sram_size = (uintptr_t)__HeapLimit > b ? (uintptr_t)__HeapLimit - b : 0;
    if (sram_size >= 1024) sram_msp = create_mspace_with_base((void *)b, sram_size, 0);
}

/* after frank_psram_init() */
void frank_heap_init(size_t psram_bytes) {
    if (!sram_msp) sram_init();
    if (!psram_bytes || psram_msp) return;   /* once: a run-time board adds it after detection */
    uintptr_t pb = ((uintptr_t)__psram_heap_start__ + 4095) & ~(uintptr_t)4095;
    psram_end = FRANK_PSRAM_BASE + psram_bytes;
    if (pb + 64 * 1024 > psram_end) return;
    psram_size = psram_end - pb;
    psram_msp = create_mspace_with_base((void *)pb, psram_size, 0);
}

void frank_heap_report(void) {
    printf("heap: SRAM %u/%u KB (peak %u), PSRAM %u/%u KB (peak %u)\n",
           (unsigned)(sram_used / 1024), (unsigned)(sram_size / 1024), (unsigned)(sram_peak / 1024),
           (unsigned)(psram_used / 1024), (unsigned)(psram_size / 1024), (unsigned)(psram_peak / 1024));
}

size_t frank_sram_free(void) { return sram_size - sram_used; }

static void *sram_nl(size_t n) {
    if (!sram_msp) sram_init();
    if (!sram_msp) return NULL;
    void *p = mspace_malloc(sram_msp, n);
    if (p) {
        sram_used += mspace_usable_size(p);
        if (sram_used > sram_peak) sram_peak = sram_used;
    }
    return p;
}

static void *psram_nl(size_t n) {
    if (!psram_msp) return NULL;
    void *p = mspace_malloc(psram_msp, n);
    if (p) {
        psram_used += mspace_usable_size(p);
        if (psram_used > psram_peak) psram_peak = psram_used;
    }
    return p;
}

#define HEAP_LOCK spin_lock_instance(PICO_SPINLOCK_ID_OS1)
static inline void lock(void) { spin_lock_unsafe_blocking(HEAP_LOCK); }
static inline void unlock(void) { spin_unlock_unsafe(HEAP_LOCK); }
#define LOCKED(expr) ({ lock(); __typeof__(expr) _r = (expr); unlock(); _r; })

void *frank_sram_malloc(size_t n) { return LOCKED(sram_nl(n)); }

/* a run-time board, after detection: under the lock, as the other core may
 * be allocating already */
void frank_heap_add_psram(size_t bytes) {
    extern void frank_heap_note_psram(size_t bytes);
    lock();
    frank_heap_init(bytes);
    unlock();
    frank_heap_note_psram(bytes);
}
void *frank_psram_malloc(size_t n) { return LOCKED(psram_nl(n)); }

static volatile int sram_only;   /* a C++ exception is being allocated: see below */

static void *heap_malloc(size_t n) {
    void *p = NULL;
    if (sram_only) return sram_nl(n);
    if (!psram_msp || n <= FRANK_SRAM_SMALL) p = sram_nl(n);
    if (!p) p = psram_nl(n);
    if (!p) p = sram_nl(n);
    return p;
}

static size_t usable(const void *p) { return mspace_usable_size(p); }

static void heap_free(void *p) {
    if (!p) return;
    if (is_psram(p)) {
        psram_used -= usable(p);
        mspace_free(psram_msp, p);
    } else if (sram_msp) {
        sram_used -= usable(p);
        mspace_free(sram_msp, p);
    }
}

void *_malloc_r(struct _reent *r, size_t n) {
    void *p = LOCKED(heap_malloc(n));
    if (!p) r->_errno = ENOMEM;
    return p;
}

void _free_r(struct _reent *r, void *p) {
    (void)r;
    lock();
    heap_free(p);
    unlock();
}

/* the copy outside the lock: growing a big buffer copied hundreds of KB with
 * the other core spinning on it */
void *_realloc_r(struct _reent *r, void *p, size_t n) {
    if (!p) return _malloc_r(r, n);
    if (!n) {
        _free_r(r, p);
        return NULL;
    }
    lock();
    const size_t old = usable(p);
    if (n <= old) {
        unlock();
        return p;
    }
    void *q = heap_malloc(n);
    unlock();
    if (!q) {
        r->_errno = ENOMEM;
        return NULL;
    }
    memcpy(q, p, old);
    _free_r(r, p);
    return q;
}

void *_calloc_r(struct _reent *r, size_t a, size_t b) {
    size_t n = a * b;
    if (b && n / b != a) {
        r->_errno = ENOMEM;
        return NULL;
    }
    void *p = _malloc_r(r, n);
    if (p) memset(p, 0, n);
    return p;
}

/* SRAM only (DMA-aligned buffers, palette pages) */
void *_memalign_r(struct _reent *r, size_t align, size_t n) {
    lock();
    if (!sram_msp) sram_init();
    void *p = sram_msp ? mspace_memalign(sram_msp, align, n) : NULL;
    if (p) {
        sram_used += usable(p);
        if (sram_used > sram_peak) sram_peak = sram_used;
    }
    unlock();
    if (!p) r->_errno = ENOMEM;
    return p;
}

size_t _malloc_usable_size_r(struct _reent *r, void *p) {
    (void)r;
    return p ? usable(p) : 0;
}

/* A thrown C++ exception never lives in PSRAM: libsupc++ counts its
 * references with LDREX/STREX, and a STREX to PSRAM never succeeds - the
 * first exception allocated there spun in __cxa_begin_catch for ever
 * (frank-ecat). Linked with -Wl,--wrap=__cxa_allocate_exception (frank.cmake
 * adds it for C++ firmware). */
extern void *__real___cxa_allocate_exception(size_t n) __attribute__((weak));
void *__wrap___cxa_allocate_exception(size_t n) {
    sram_only++;
    void *p = __real___cxa_allocate_exception(n);
    sram_only--;
    return p;
}
