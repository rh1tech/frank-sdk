#pragma once
#include "pico.h"
#include <unistd.h>
static inline void __wfi(void) { usleep(1000); }
static inline void __dmb(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t s) { (void)s; }
