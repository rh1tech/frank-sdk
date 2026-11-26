/* frank-sdk host build: a "board" with everything an SDL window can stand in for. */
#pragma once
#define FRANK_HOST 1
#define FRANK_BOARD_HOST 1
#define FRANK_BOARD_ID "HOST"
#define FRANK_BOARD_NAME "host (SDL)"
#define FRANK_TARGET_ROLE_SINGLE 1
#define FRANK_CPU_MHZ 504
#define FRANK_CONSOLE_USB 0
#define FRANK_HAS_PSRAM 1
#define FRANK_PSRAM_CS 0
#define FRANK_PSRAM_BYTES (8 * 1024 * 1024)
#define FRANK_HAS_VIDEO 1
#define FRANK_HAS_VIDEO_HDMI 1
#define FRANK_HAS_SD 1
#define FRANK_HAS_AUDIO 1
