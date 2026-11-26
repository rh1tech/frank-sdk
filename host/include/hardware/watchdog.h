/* frank-sdk host build: the scratch registers survive a "reboot" (a restart of
 * the firmware thread), as on the chip. */
#pragma once
#include "pico.h"
typedef struct { uint32_t scratch[8]; uint32_t reason; } host_watchdog_t;
extern host_watchdog_t host_watchdog;
#define watchdog_hw (&host_watchdog)
static inline void watchdog_update(void) {}
static inline void watchdog_enable(uint32_t ms, bool pause) { (void)ms; (void)pause; }
static inline bool watchdog_caused_reboot(void) { return false; }
