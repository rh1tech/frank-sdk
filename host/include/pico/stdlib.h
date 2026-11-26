/* frank-sdk host build: time, sleep and no-op GPIO. */
#pragma once
#include "pico.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>
typedef uint64_t absolute_time_t;
static inline uint64_t time_us_64(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}
static inline uint32_t time_us_32(void) { return (uint32_t)time_us_64(); }
static inline absolute_time_t get_absolute_time(void) { return time_us_64(); }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms) { return time_us_64() + ms * 1000ull; }
static inline bool time_reached(absolute_time_t t) { return time_us_64() >= t; }
static inline void sleep_ms(uint32_t ms) { usleep(ms * 1000u); }
static inline void sleep_us(uint64_t us) { usleep((useconds_t)us); }
static inline void busy_wait_ms(uint32_t ms) { usleep(ms * 1000u); }
static inline void busy_wait_us(uint64_t us) { usleep((useconds_t)us); }
static inline void tight_loop_contents(void) {}
int getchar_timeout_us(uint32_t us);
#define GPIO_OUT 1
#define GPIO_IN 0
static inline void gpio_init(uint p) { (void)p; }
static inline void gpio_set_dir(uint p, bool o) { (void)p; (void)o; }
static inline void gpio_put(uint p, bool v) { (void)p; (void)v; }
static inline void gpio_xor_mask(uint32_t m) { (void)m; }
static inline void stdio_init_all(void) {}
