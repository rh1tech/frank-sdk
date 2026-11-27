/*
 * frank-sdk: frank-test's composite driver built as frank-test builds it: the
 * rename and SRAM memset/memcpy shims force-included, the pointer-type
 * warnings of the original (murmnes, frank-msx) code silenced.
 */
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#pragma GCC diagnostic ignored "-Wall"
/* its graphics_* become tv_graphics_*: a firmware has its own graphics_* API */
#include "tv_rename.h"
#include "tv_mem.h"
#include "tv-software.c"
