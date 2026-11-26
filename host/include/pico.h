/* frank-sdk host build: the bits of pico.h firmware code touches. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "frank_host_board.h"
#define __not_in_flash_func(f) f
#define __no_inline_not_in_flash_func(f) f
#define __not_in_flash(s)
#define __scratch_x(s)
#define __scratch_y(s)
#define __time_critical_func(f) f
typedef unsigned int uint;
